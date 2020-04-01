#pragma once

#include <angelscript.h>

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <angelscript-llvm/detail/builder.hpp>
#include <map>

namespace asllvm::detail
{

class ModuleBuilder;

struct HandledInstruction
{
	std::size_t read_bytes;
	bool was_recognized : 1;
};

class FunctionBuilder
{
public:
	FunctionBuilder(Builder& builder, ModuleBuilder& module_builder, llvm::Function* function);

	HandledInstruction read_instruction(const asDWORD* bytecode);

private:
	llvm::Value* load_stack_value(short i, llvm::Type* type);
	void store_stack_value(short i, llvm::Value* value);

	llvm::AllocaInst* get_stack_variable(short i, llvm::Type* type);
	llvm::AllocaInst* allocate_stack_variable(short i, llvm::Type* type);

	llvm::Argument* get_argument(std::size_t i);

	Builder* m_builder;
	ModuleBuilder* m_module_builder;
	llvm::Function* m_function;
	llvm::BasicBlock* m_entry_block;
	std::map<short, llvm::AllocaInst*> m_local_variables;
	bool m_return_emitted = false;
};

}
