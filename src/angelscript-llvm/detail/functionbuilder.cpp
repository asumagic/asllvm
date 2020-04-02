#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <angelscript-llvm/jit.hpp>

#include <array>
#include <fmt/core.h>

namespace asllvm::detail
{
FunctionBuilder::FunctionBuilder(
	JitCompiler&       compiler,
	ModuleBuilder&     module_builder,
	asIScriptFunction& script_function,
	llvm::Function*    llvm_function) :
	m_compiler{compiler},
	m_module_builder{module_builder},
	m_script_function{script_function},
	m_llvm_function{llvm_function},
	m_entry_block{llvm::BasicBlock::Create(m_compiler.builder().context(), "entry", llvm_function)}
{
	m_compiler.builder().ir_builder().SetInsertPoint(m_entry_block);
}

void FunctionBuilder::read_bytecode(asDWORD* bytecode, asUINT length)
{
	const auto walk_bytecode = [&](auto&& func) {
		asDWORD* bytecode_current = bytecode;
		asDWORD* bytecode_end     = bytecode + length;

		while (bytecode_current < bytecode_end)
		{
			const asSBCInfo   info             = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode_current)];
			const std::size_t instruction_size = asBCTypeSize[info.type];

			func(bytecode_current);

			bytecode_current += instruction_size;
		}
	};

	// TODO: move to create_function()
	{
		std::size_t parameter_count      = m_script_function.GetParamCount();
		short       current_stack_offset = 0;
		for (std::size_t i = 0; i < parameter_count; ++i)
		{
			int         type_id = 0;
			const char* name    = nullptr;
			m_script_function.GetParam(i, &type_id, nullptr, &name);

			llvm::Type* type = m_compiler.builder().script_type_to_llvm_type(type_id);

			llvm::Argument* llvm_argument = &*(m_llvm_function->arg_begin() + i);

			if (name != nullptr)
			{
				llvm_argument->setName(name);
			}

			m_parameter_offsets.emplace(i, current_stack_offset);
			m_variables.emplace(current_stack_offset, llvm_argument);

			current_stack_offset -= m_compiler.builder().is_script_type_64(type_id) ? 2 : 1;
		}
	}

	walk_bytecode([this](auto* bytecode) { return preprocess_instruction(bytecode); });
	walk_bytecode([this](auto* bytecode) { return read_instruction(bytecode); });
}

llvm::Function* FunctionBuilder::create_wrapper_function()
{
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir_builder();
	llvm::LLVMContext& context = m_compiler.builder().context();
	CommonDefinitions& defs    = m_compiler.builder().definitions();

	const std::array<llvm::Type*, 2> types{defs.vm_registers->getPointerTo(), llvm::IntegerType::getInt64Ty(context)};

	llvm::Type* return_type = llvm::Type::getVoidTy(context);

	llvm::Function* wrapper_function = llvm::Function::Create(
		llvm::FunctionType::get(return_type, types, false),
		llvm::Function::ExternalLinkage,
		make_function_name(m_script_function.GetName(), m_script_function.GetNamespace()) + ".jitentry",
		m_module_builder.module());

	wrapper_function->setCallingConv(llvm::CallingConv::C);

	llvm::BasicBlock* block = llvm::BasicBlock::Create(context, "entry", wrapper_function);

	ir.SetInsertPoint(block);

	llvm::Argument* registers = &*(wrapper_function->arg_begin() + 0);
	registers->setName("vmregs");

	llvm::Argument* arg = &*(wrapper_function->arg_begin() + 1);
	arg->setName("jitarg");

	llvm::Value* pp = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0),
											llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0)};

		return ir.CreateGEP(registers, indices);
	}();

	llvm::Value* fp = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0),
											llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 1)};

		auto* pointer = ir.CreateGEP(registers, indices);
		return ir.CreateLoad(llvm::Type::getInt32Ty(context)->getPointerTo(), pointer, "stackFramePointer");
	}();

	llvm::Value* value_register = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0),
											llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 3)};

		return ir.CreateGEP(registers, indices, "valueRegister");
	}();

	std::vector<llvm::Value*> args(m_script_function.GetParamCount());

	for (short i = 0; i < m_script_function.GetParamCount(); ++i)
	{
		auto it = m_parameter_offsets.find(i);
		if (it == m_parameter_offsets.end())
		{
			throw std::runtime_error{"expected parameter to be mapped - did earlier read_bytecode() fail?"};
		}

		std::array<llvm::Value*, 1> indices{llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), -it->second)};

		llvm::Type* arg_type = (m_llvm_function->arg_begin() + i)->getType();

		llvm::Value* pointer = ir.CreateGEP(fp, indices);
		pointer              = ir.CreateBitCast(pointer, arg_type->getPointerTo());
		args[i]              = ir.CreateLoad(arg_type, pointer);
	}

	auto* ret = ir.CreateCall(m_llvm_function, args, "ret");

	if (m_llvm_function->getReturnType() != llvm::Type::getVoidTy(context))
	{
		ir.CreateStore(ret, value_register);
	}

	// Set the program pointer to the RET instruction
	auto* ret_ptr_value = ir.CreateBitCast(
		llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), reinterpret_cast<std::uintptr_t>(m_ret_pointer)),
		llvm::PointerType::getInt32PtrTy(context),
		"retaddr");
	ir.CreateStore(ret_ptr_value, pp);

	ir.CreateRetVoid();

	return wrapper_function;
}

void FunctionBuilder::preprocess_instruction(asDWORD* bytecode)
{
	asIScriptEngine& engine = *m_script_function.GetEngine();

	const asSBCInfo info = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode)];

	switch (info.bc)
	{
	// These instructions do not write to the stack
	case asBC_JitEntry:
	case asBC_SUSPEND:
	case asBC_CpyVtoR4:
	case asBC_CpyVtoR8:
	case asBC_RET:
	{
		break;
	}

	// These instructions write to the stack, always at the first sword
	case asBC_ADDi:
	case asBC_ADDi64:
	case asBC_CpyVtoV4:
	case asBC_CpyVtoV8:
	case asBC_sbTOi:
	case asBC_swTOi:
	case asBC_iTOb:
	case asBC_iTOw:
	{
		auto target = asBC_SWORDARG0(bytecode);
		reserve_variable(target);
		break;
	}

	default:
	{
		throw std::runtime_error{fmt::format("cannot preprocess unrecognized instruction '{}'", info.name)};
	}
	}
}

void FunctionBuilder::read_instruction(asDWORD* bytecode)
{
	asIScriptEngine&   engine  = *m_script_function.GetEngine();
	llvm::LLVMContext& context = m_compiler.builder().context();

	const asSBCInfo info = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode)];

	if (info.bc != asBC_RET && m_return_emitted)
	{
		throw std::runtime_error{"ir ret was emitted already - we expect RET immediatelly after CpyVToR"};
	}

	switch (info.bc)
	{
	case asBC_JitEntry:
	{
		if (m_compiler.config().verbose)
		{
			m_compiler.diagnostic(engine, "Found JIT entry point, patching as valid entry point");
		}

		// If this argument is zero (the default), the script engine will never call into the JIT.
		asBC_PTRARG(bytecode) = 1;

		break;
	}

	case asBC_SUSPEND:
	{
		if (m_compiler.config().verbose)
		{
			m_compiler.diagnostic(engine, "Found VM suspend, these are unsupported and ignored", asMSGTYPE_WARNING);
		}

		break;
	}

	case asBC_ADDi:
	{
		auto target = asBC_SWORDARG0(bytecode);
		auto a      = asBC_SWORDARG1(bytecode);
		auto b      = asBC_SWORDARG2(bytecode);

		llvm::Type* type = llvm::IntegerType::getInt32Ty(context);

		store_stack_value(
			target, m_compiler.builder().ir_builder().CreateAdd(load_stack_value(a, type), load_stack_value(b, type)));

		break;
	}

	case asBC_ADDi64:
	{
		auto target = asBC_SWORDARG0(bytecode);
		auto a      = asBC_SWORDARG1(bytecode);
		auto b      = asBC_SWORDARG2(bytecode);

		llvm::Type* type = llvm::IntegerType::getInt64Ty(context);

		store_stack_value(
			target, m_compiler.builder().ir_builder().CreateAdd(load_stack_value(a, type), load_stack_value(b, type)));

		break;
	}

	case asBC_CpyVtoV4:
	{
		auto target = asBC_SWORDARG0(bytecode);
		auto source = asBC_SWORDARG1(bytecode);

		llvm::Type* type = llvm::IntegerType::getInt32Ty(context);
		store_stack_value(target, load_stack_value(source, type));

		break;
	}

	case asBC_CpyVtoV8:
	{
		auto target = asBC_SWORDARG0(bytecode);
		auto source = asBC_SWORDARG1(bytecode);

		llvm::Type* type = llvm::IntegerType::getInt64Ty(context);
		store_stack_value(target, load_stack_value(source, type));

		break;
	}

	case asBC_CpyVtoR4:
	case asBC_CpyVtoR8:
	{
		m_return_emitted = true;

		m_compiler.builder().ir_builder().CreateRet(
			load_stack_value(asBC_SWORDARG0(bytecode), m_llvm_function->getReturnType()));

		break;
	}

	case asBC_sbTOi:
	{
		// TODO: sign extend should not be done on unsigned types

		m_compiler.builder().ir_builder().CreateSExt(
			load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt8Ty(context)),
			llvm::IntegerType::getInt32Ty(context));

		break;
	}

	case asBC_swTOi:
	{
		// TODO: sign extend should not be done on unsigned types

		m_compiler.builder().ir_builder().CreateSExt(
			load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt16Ty(context)),
			llvm::IntegerType::getInt32Ty(context));

		break;
	}

	case asBC_iTOb:
	{
		m_compiler.builder().ir_builder().CreateTrunc(
			load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt32Ty(context)),
			llvm::IntegerType::getInt8Ty(context));

		break;
	}

	case asBC_iTOw:
	{
		m_compiler.builder().ir_builder().CreateTrunc(
			load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt32Ty(context)),
			llvm::IntegerType::getInt16Ty(context));

		break;
	}

	case asBC_RET:
	{
		if (!m_return_emitted)
		{
			m_compiler.builder().ir_builder().CreateRetVoid();
		}

		m_ret_pointer    = bytecode;
		m_return_emitted = true;
		break;
	}

	default:
	{
		throw std::runtime_error{fmt::format("could not recognize bytecode instruction '{}'", info.name)};
	}
	}
}

llvm::Value* FunctionBuilder::load_stack_value(StackVariableIdentifier i, llvm::Type* type)
{
	if (i > 0)
	{
		llvm::Value* variable = get_stack_variable(i, type);
		return m_compiler.builder().ir_builder().CreateLoad(type, variable);
	}

	auto it = m_variables.find(i);
	if (it == m_variables.end())
	{
		throw std::runtime_error{"parameter unexpectedly not found"};
	}

	return &*it->second;
}

void FunctionBuilder::store_stack_value(StackVariableIdentifier i, llvm::Value* value)
{
	if (i <= 0)
	{
		throw std::runtime_error{"storing to parameters unsupported for now"};
	}
	else
	{
		llvm::Value* variable = get_stack_variable(i, value->getType());
		m_compiler.builder().ir_builder().CreateStore(value, variable);
	}
}

llvm::Value* FunctionBuilder::get_stack_variable(StackVariableIdentifier i, llvm::Type* type)
{
	auto it = m_variables.find(i);
	if (it == m_variables.end())
	{
		throw std::runtime_error{"variable unexpectedly not allocated"};
	}

	return m_compiler.builder().ir_builder().CreateBitCast(it->second, type->getPointerTo());
}

void FunctionBuilder::reserve_variable(StackVariableIdentifier id)
{
	if (id < 0)
	{
		return;
	}

	for (std::size_t i = m_highest_allocated + 1; i <= id; ++i)
	{
		m_variables.emplace(
			i,
			m_compiler.builder().ir_builder().CreateAlloca(
				llvm::IntegerType::getInt64Ty(m_compiler.builder().context())));
	}

	m_highest_allocated = id;
}

} // namespace asllvm::detail
