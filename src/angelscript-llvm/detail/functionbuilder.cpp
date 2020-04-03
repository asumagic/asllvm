#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <angelscript-llvm/detail/jitcompiler.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>

#include <array>
#include <fmt/core.h>

// Include order matters apparently (probably an AS bug: as_memory.h seems to be required by as_array.h)
// clang-format off
#include <as_memory.h>
#include <as_callfunc.h>
#include <as_scriptfunction.h>
// clang-format on

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
	m_compiler.builder().ir().SetInsertPoint(m_entry_block);
}

llvm::Function* FunctionBuilder::read_bytecode(asDWORD* bytecode, asUINT length)
{
	if (m_script_function.GetParamCount() != 0)
	{
		int type_id;
		m_script_function.GetParam(0, &type_id);
		m_locals_offset = m_compiler.builder().get_script_type_dword_size(type_id);
	}

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

	try
	{
		walk_bytecode([this](auto* bytecode) { return preprocess_instruction(bytecode); });

		{
			llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
			llvm::LLVMContext& context = m_compiler.builder().context();

			m_locals = ir.CreateAlloca(
				llvm::ArrayType::get(llvm::IntegerType::getInt32Ty(context), m_locals_size + m_max_extra_stack_size),
				0,
				"locals");
			m_value = ir.CreateAlloca(llvm::IntegerType::getInt64Ty(context), 0, "valuereg");

			m_stack_pointer = m_locals_size + m_max_extra_stack_size;
		}

		walk_bytecode([this](auto* bytecode) { return read_instruction(bytecode); });
	}
	catch (std::exception& exception)
	{
		m_llvm_function->removeFromParent();
		throw;
	}

	return m_llvm_function;
}

llvm::Function* FunctionBuilder::create_wrapper_function()
{
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
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

		return ir.CreateGEP(registers, indices, "programPointer");
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

	std::array<llvm::Value*, 1> args{{fp}};

	llvm::Value* ret = ir.CreateCall(m_llvm_function, args);

	if (m_llvm_function->getReturnType() != llvm::Type::getVoidTy(context))
	{
		ir.CreateStore(ret, ir.CreateBitCast(value_register, ret->getType()->getPointerTo()));
	}

	// Set the program pointer to the RET instruction
	auto* ret_ptr_value = ir.CreateIntToPtr(
		llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), reinterpret_cast<std::uintptr_t>(m_ret_pointer)),
		llvm::IntegerType::getInt32PtrTy(context));
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
	case asBC_CALLSYS:
	case asBC_CALL:
	{
		break;
	}

	// These instructions write to the stack, always at the first sword
	case asBC_ADDi:
	case asBC_ADDi64:
	case asBC_CpyVtoV4:
	case asBC_CpyVtoV8:
	case asBC_CpyRtoV4:
	case asBC_CpyRtoV8:
	case asBC_sbTOi:
	case asBC_swTOi:
	case asBC_iTOb:
	case asBC_iTOw:
	case asBC_iTOi64:
	case asBC_i64TOi:
	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:
	{
		auto target   = asBC_SWORDARG0(bytecode);
		m_locals_size = std::max(m_locals_size, long(target) + 2);
		break;
	}

	// These instructions always write a pointer to the stack
	case asBC_PGA:
	case asBC_PSF:
	{
		m_max_extra_stack_size += AS_PTR_SIZE;
		break;
	}

	// These instructions always write a 32-bit value to the stack
	case asBC_PshV4:
	case asBC_PshC4:
	{
		++m_max_extra_stack_size;
		break;
	}

	// These instructions always write a 64-bit value to the stack
	case asBC_PshV8:
	case asBC_PshC8:
	{
		m_max_extra_stack_size += 2;
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
	asIScriptEngine&   engine  = m_compiler.engine();
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
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

		// Pass the JitCompiler as the jitArg value, which can be used by lazy_jit_compiler().
		// TODO: this is probably UB
		asBC_PTRARG(bytecode) = reinterpret_cast<asPWORD>(&m_compiler);

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

		store_stack_value(target, ir.CreateAdd(load_stack_value(a, type), load_stack_value(b, type)));

		break;
	}

	case asBC_ADDi64:
	{
		auto target = asBC_SWORDARG0(bytecode);
		auto a      = asBC_SWORDARG1(bytecode);
		auto b      = asBC_SWORDARG2(bytecode);

		llvm::Type* type = llvm::IntegerType::getInt64Ty(context);

		store_stack_value(target, ir.CreateAdd(load_stack_value(a, type), load_stack_value(b, type)));

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

		ir.CreateRet(load_stack_value(asBC_SWORDARG0(bytecode), m_llvm_function->getReturnType()));

		break;
	}

	case asBC_CpyRtoV4:
	{
		llvm::Value* value_i32_ptr = ir.CreateBitCast(m_value, llvm::Type::getInt32PtrTy(context));
		llvm::Value* value_i32     = ir.CreateLoad(llvm::Type::getInt32Ty(context), value_i32_ptr);

		store_stack_value(asBC_SWORDARG0(bytecode), value_i32);

		break;
	}

	case asBC_CpyRtoV8:
	{
		llvm::Value* value_i64_ptr = ir.CreateBitCast(m_value, llvm::Type::getInt64PtrTy(context));
		llvm::Value* value_i64     = ir.CreateLoad(llvm::Type::getInt64Ty(context), value_i64_ptr);

		store_stack_value(asBC_SWORDARG0(bytecode), value_i64);

		break;
	}

	case asBC_sbTOi:
	{
		// TODO: sign extend should not be done on unsigned types

		store_stack_value(
			asBC_SWORDARG0(bytecode),
			ir.CreateSExt(
				load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt8Ty(context)),
				llvm::IntegerType::getInt32Ty(context)));

		break;
	}

	case asBC_swTOi:
	{
		// TODO: sign extend should not be done on unsigned types

		store_stack_value(
			asBC_SWORDARG0(bytecode),
			ir.CreateSExt(
				load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt16Ty(context)),
				llvm::IntegerType::getInt32Ty(context)));

		break;
	}

	case asBC_iTOb:
	{
		store_stack_value(
			asBC_SWORDARG0(bytecode),
			ir.CreateTrunc(
				load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt32Ty(context)),
				llvm::IntegerType::getInt8Ty(context)));

		break;
	}

	case asBC_iTOw:
	{
		store_stack_value(
			asBC_SWORDARG0(bytecode),
			ir.CreateTrunc(
				load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt32Ty(context)),
				llvm::IntegerType::getInt16Ty(context)));

		break;
	}

	case asBC_iTOi64:
	{
		// TODO: sign extend should not be done on unsigned types

		store_stack_value(
			asBC_SWORDARG0(bytecode),
			ir.CreateSExt(
				load_stack_value(asBC_SWORDARG1(bytecode), llvm::Type::getInt32Ty(context)),
				llvm::Type::getInt64Ty(context)));
		break;
	}

	case asBC_i64TOi:
	{
		store_stack_value(
			asBC_SWORDARG0(bytecode),
			ir.CreateTrunc(
				load_stack_value(asBC_SWORDARG1(bytecode), llvm::IntegerType::getInt64Ty(context)),
				llvm::IntegerType::getInt32Ty(context)));

		break;
	}

	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:
	{
		store_stack_value(
			asBC_SWORDARG0(bytecode), llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), asBC_DWORDARG(bytecode)));

		break;
	}

	case asBC_PGA:
	{
		m_stack_pointer -= AS_PTR_SIZE;
		store_stack_value(
			m_stack_pointer, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), asBC_PTRARG(bytecode)));
		break;
	}

	case asBC_PSF:
	{
		m_stack_pointer -= AS_PTR_SIZE;
		store_stack_value(
			m_stack_pointer, load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt64Ty(context)));
		break;
	}

	case asBC_PshV4:
	{
		--m_stack_pointer;
		store_stack_value(
			m_stack_pointer, load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt32Ty(context)));
		break;
	}

	case asBC_PshV8:
	{
		m_stack_pointer -= 2;
		store_stack_value(
			m_stack_pointer, load_stack_value(asBC_SWORDARG0(bytecode), llvm::IntegerType::getInt64Ty(context)));
		break;
	}

	case asBC_PshC4:
	{
		--m_stack_pointer;
		store_stack_value(
			m_stack_pointer, llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(context), asBC_DWORDARG(bytecode)));
		break;
	}

	case asBC_PshC8:
	{
		m_stack_pointer -= 2;
		store_stack_value(
			m_stack_pointer, llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), asBC_QWORDARG(bytecode)));
		break;
	}

	case asBC_CALLSYS:
	{
		asIScriptFunction* function = engine.GetFunctionById(asBC_INTARG(bytecode));

		if (function == nullptr)
		{
			throw std::runtime_error{"function referenced in CALLSYS unexpectedly null"};
		}

		emit_system_call(*function);
		break;
	}

	case asBC_CALL:
	{
		asIScriptFunction* function = engine.GetFunctionById(asBC_INTARG(bytecode));

		llvm::Value* new_frame_pointer = get_stack_value_pointer(m_stack_pointer, llvm::Type::getInt32Ty(context));
		std::array<llvm::Value*, 1> args{{new_frame_pointer}};

		llvm::Function* callee = m_module_builder.create_function(*function);

		llvm::Value* ret = ir.CreateCall(callee->getFunctionType(), callee, args);

		if (callee->getReturnType() != llvm::Type::getVoidTy(context))
		{
			// Store to the value register
			llvm::Value* typed_value_register = ir.CreateBitCast(m_value, ret->getType()->getPointerTo());
			ir.CreateStore(ret, typed_value_register);
		}

		{
			m_stack_pointer += static_cast<asCScriptFunction&>(*function).GetSpaceNeededForArguments();
		}

		break;
	}

	case asBC_RET:
	{
		if (!m_return_emitted)
		{
			ir.CreateRetVoid();
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

void FunctionBuilder::emit_system_call(asIScriptFunction& function)
{
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
	llvm::LLVMContext& context = m_compiler.builder().context();

	llvm::Function* callee = m_module_builder.get_system_function(function);

	const std::size_t         argument_count = callee->arg_size();
	std::vector<llvm::Value*> args(callee->arg_size());

	{
		long current_parameter_offset = m_stack_pointer;
		for (std::size_t i = 0; i < argument_count; ++i)
		{
			int type_id = 0;
			if (function.GetParam(i, &type_id) < 0)
			{
				throw std::runtime_error{
					"something went wrong during registering of system function: nth parameter does not appear to "
					"exist in the script function"};
			}

			llvm::Argument* llvm_argument = &*(callee->arg_begin() + i);
			args[i]                       = load_stack_value(current_parameter_offset, llvm_argument->getType());

			current_parameter_offset -= m_compiler.builder().get_script_type_dword_size(type_id);
		}
	}

	llvm::Value* returned = ir.CreateCall(callee->getFunctionType(), callee, args);

	// todo: store returned m_value
}

llvm::Value* FunctionBuilder::load_stack_value(StackVariableIdentifier i, llvm::Type* type)
{
	return m_compiler.builder().ir().CreateLoad(type, get_stack_value_pointer(i, type));
}

void FunctionBuilder::store_stack_value(StackVariableIdentifier i, llvm::Value* value)
{
	m_compiler.builder().ir().CreateStore(value, get_stack_value_pointer(i, value->getType()));
}

llvm::Value* FunctionBuilder::get_stack_value_pointer(FunctionBuilder::StackVariableIdentifier i, llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	llvm::Value* pointer = get_stack_value_pointer(i);
	return ir.CreateBitCast(pointer, type->getPointerTo());
}

llvm::Value* FunctionBuilder::get_stack_value_pointer(FunctionBuilder::StackVariableIdentifier i)
{
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
	llvm::LLVMContext& context = m_compiler.builder().context();

	// Get a pointer to that argument
	if (i < m_locals_offset)
	{
		std::array<llvm::Value*, 1> indices{{llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), -i)}};

		return ir.CreateGEP(&*(m_llvm_function->arg_begin() + 0), indices);
	}

	// Get a pointer to that value on the local stack
	{
		const std::size_t local_offset = i - m_locals_offset;

		std::array<llvm::Value*, 2> indices{
			{llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), 0),
			 llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), local_offset)}};

		return ir.CreateGEP(m_locals, indices);
	}
}
} // namespace asllvm::detail
