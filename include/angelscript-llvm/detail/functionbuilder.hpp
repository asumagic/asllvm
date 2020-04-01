#pragma once

#include <angelscript-llvm/detail/fwd.hpp>
#include <angelscript.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <vector>

namespace asllvm::detail
{
class FunctionBuilder
{
	public:
	FunctionBuilder(
		JitCompiler&       compiler,
		ModuleBuilder&     module_builder,
		asIScriptFunction& script_function,
		llvm::Function*    llvm_function);

	void read_bytecode(asDWORD* bytecode, asUINT length);

	private:
	void         preprocess_instruction(asDWORD* bytecode);
	void         read_instruction(asDWORD* bytecode);
	llvm::Value* load_stack_value(short i, llvm::Type* type);
	void         store_stack_value(short i, llvm::Value* value);

	llvm::Value*      get_stack_variable(short i, llvm::Type* type);
	llvm::AllocaInst* allocate_stack_variable(short i);
	void              reserve_variable(short count);

	llvm::Argument* get_argument(std::size_t i);

	JitCompiler&   m_compiler;
	ModuleBuilder& m_module_builder;

	asIScriptFunction& m_script_function;

	llvm::Function*                m_llvm_function;
	llvm::BasicBlock*              m_entry_block;
	bool                           m_return_emitted = false;
	std::vector<llvm::AllocaInst*> m_allocated_variables;
};

} // namespace asllvm::detail
