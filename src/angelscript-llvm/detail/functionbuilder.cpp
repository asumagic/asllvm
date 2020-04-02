#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/jit.hpp>

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
	m_entry_block{llvm::BasicBlock::Create(context, "entry", llvm_function)}
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
		std::size_t parameter_count = m_script_function.GetParamCount();
		for (std::size_t i = 0; i < parameter_count; ++i)
		{
			int         type_id = 0;
			const char* name    = nullptr;
			m_script_function.GetParam(i, &type_id, nullptr, &name);

			llvm::Type* type = m_compiler.builder().script_type_to_llvm_type(type_id);

			llvm::Argument* llvm_argument = get_argument(i);

			if (name != nullptr)
			{
				llvm_argument->setName(name);
			}
		}
	}

	walk_bytecode([this](auto* bytecode) { return preprocess_instruction(bytecode); });
	walk_bytecode([this](auto* bytecode) { return read_instruction(bytecode); });
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
	asIScriptEngine& engine = *m_script_function.GetEngine();

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
	else
	{
		return get_argument(std::size_t(-i));
	}
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
	if (i < 0)
	{
		return get_argument(-i);
	}

	return m_compiler.builder().ir_builder().CreateBitCast(m_allocated_variables.at(i - 1), type->getPointerTo());
}

void FunctionBuilder::reserve_variable(StackVariableIdentifier count)
{
	if (count < 0)
	{
		return;
	}

	for (std::size_t i = m_allocated_variables.size(); i < count; ++i)
	{
		m_allocated_variables.emplace_back(
			m_compiler.builder().ir_builder().CreateAlloca(llvm::IntegerType::getInt64Ty(context)));
	}
}

llvm::Argument* FunctionBuilder::get_argument(std::size_t i) { return &*(m_llvm_function->args().begin() + i); }

} // namespace asllvm::detail
