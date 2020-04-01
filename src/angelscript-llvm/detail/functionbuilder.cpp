#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>

namespace asllvm::detail
{

FunctionBuilder::FunctionBuilder(Builder& builder, ModuleBuilder& module_builder, llvm::Function* function) :
	m_builder{&builder},
	m_module_builder{&module_builder},
	m_function{function},
	m_entry_block{llvm::BasicBlock::Create(context, "entry", function)}
{
	m_builder->ir_builder().SetInsertPoint(m_entry_block);
}

HandledInstruction FunctionBuilder::read_instruction(const asDWORD* bytecode)
{
	const asSBCInfo info = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode)];
	const std::size_t instruction_size = asBCTypeSize[info.type];

	HandledInstruction handled;
	handled.was_recognized = true;
	handled.read_bytes = instruction_size;

	if (info.bc != asBC_RET && m_return_emitted)
	{
		throw std::runtime_error{"ir ret was emitted already - we expect RET immediatelly after CpyVToR"};
	}

	switch (info.bc)
	{
	case asBC_ADDi:
	{
		auto target = asBC_SWORDARG0(bytecode);
		auto a = asBC_SWORDARG1(bytecode);
		auto b = asBC_SWORDARG2(bytecode);

		llvm::Type* type = llvm::IntegerType::getInt32Ty(context);

		store_stack_value(target, m_builder->ir_builder().CreateAdd(load_stack_value(a, type), load_stack_value(b, type)));

		break;
	}

	case asBC_CpyVtoR4:
	{
		m_return_emitted = true;

		llvm::Type* type = llvm::IntegerType::getInt32Ty(context);
		m_builder->ir_builder().CreateRet(load_stack_value(asBC_SWORDARG0(bytecode), type));

		break;
	}

	case asBC_RET:
	{
		if (!m_return_emitted)
		{
			m_builder->ir_builder().CreateRetVoid();
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
		return m_builder->ir_builder().CreateLoad(type, variable);
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
		m_builder->ir_builder().CreateStore(value, variable);
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
	auto emplace_result = m_local_variables.emplace(i, m_builder->ir_builder().CreateAlloca(type));

	if (!emplace_result.second)
	{
		throw std::runtime_error{"tried to allocate stack variable twice"};
	}

	return emplace_result.first->second;
}

llvm::Argument* FunctionBuilder::get_argument(std::size_t i)
{
	return &*(m_function->args().begin() + i);
}

}
