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
	asIScriptEngine& engine = *m_script_function.GetEngine();

	const asDWORD* bytecode_current = bytecode;
	const asDWORD* bytecode_end     = bytecode + length;

	while (bytecode_current < bytecode_end)
	{
		const asSBCInfo   info             = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode_current)];
		const std::size_t instruction_size = asBCTypeSize[info.type];

		switch (info.bc)
		{
		case asBC_JitEntry:
		{
			if (m_compiler.config().verbose)
			{
				m_compiler.diagnostic(engine, "Found JIT entry point, patching as valid entry point");
			}

			// If this argument is zero (the default), the script engine will never call into the JIT.
			asBC_PTRARG(bytecode_current) = 1;

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

		default:
		{
			HandledInstruction handled = read_instruction(bytecode_current);

			if (!handled.was_recognized)
			{
				throw std::runtime_error{fmt::format("hit unrecognized bytecode instruction {}, aborting", info.name)};
			}
		}
		}

		bytecode_current += instruction_size;
	}
}

FunctionBuilder::HandledInstruction FunctionBuilder::read_instruction(const asDWORD* bytecode)
{
	const asSBCInfo   info             = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode)];
	const std::size_t instruction_size = asBCTypeSize[info.type];

	HandledInstruction handled;
	handled.was_recognized = true;
	handled.read_bytes     = instruction_size;

	if (info.bc != asBC_RET && m_return_emitted)
	{
		throw std::runtime_error{"ir ret was emitted already - we expect RET immediatelly after CpyVToR"};
	}

	switch (info.bc)
	{
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

	case asBC_CpyVtoV4:
	{
		auto target = asBC_SWORDARG0(bytecode);
		auto source = asBC_SWORDARG1(bytecode);

		// TODO: this is probably an incorrect assumption: how does this work for user objects and floats?
		llvm::Type* type = llvm::IntegerType::getInt32Ty(context);
		store_stack_value(target, load_stack_value(source, type));

		break;
	}

	case asBC_CpyVtoR4:
	{
		m_return_emitted = true;

		// TODO: this is probably an incorrect assumption: how does this work for user objects and floats?
		llvm::Type* type = llvm::IntegerType::getInt32Ty(context);
		m_compiler.builder().ir_builder().CreateRet(load_stack_value(asBC_SWORDARG0(bytecode), type));

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
		handled.was_recognized = false;
		break;
	}
	}

	return handled;
}

llvm::Value* FunctionBuilder::load_stack_value(short i, llvm::Type* type)
{
	if (i > 0)
	{
		llvm::AllocaInst* variable = get_stack_variable(i, type);
		return m_compiler.builder().ir_builder().CreateLoad(type, variable);
	}
	else
	{
		return get_argument(std::size_t(-i));
	}
}

void FunctionBuilder::store_stack_value(short i, llvm::Value* value)
{
	if (i <= 0)
	{
		throw std::runtime_error{"storing to parameters unsupported for now"};
	}
	else
	{
		llvm::AllocaInst* variable = get_stack_variable(i, value->getType());
		m_compiler.builder().ir_builder().CreateStore(value, variable);
	}
}

llvm::AllocaInst* FunctionBuilder::get_stack_variable(short i, llvm::Type* type)
{
	auto it = m_local_variables.find(i);

	if (it == m_local_variables.end())
	{
		return allocate_stack_variable(i, type);
	}

	// TODO: check if this is the correct way
	if (it->second->getType() != type->getPointerTo())
	{
		throw std::runtime_error{"cannot reuse stack variable with this type"};
	}

	return it->second;
}

llvm::AllocaInst* FunctionBuilder::allocate_stack_variable(short i, llvm::Type* type)
{
	auto emplace_result = m_local_variables.emplace(i, m_compiler.builder().ir_builder().CreateAlloca(type));

	if (!emplace_result.second)
	{
		throw std::runtime_error{"tried to allocate stack variable twice"};
	}

	return emplace_result.first->second;
}

llvm::Argument* FunctionBuilder::get_argument(std::size_t i) { return &*(m_llvm_function->args().begin() + i); }

} // namespace asllvm::detail
