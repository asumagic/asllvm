#pragma once

#include <angelscript.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <map>

namespace asllvm
{
class JitCompiler;
}

namespace asllvm::detail
{
class Builder;
class ModuleBuilder;

struct HandledInstruction
{
	std::size_t read_bytes;
	bool        was_recognized : 1;
};

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
	HandledInstruction read_instruction(const asDWORD* bytecode);
	llvm::Value*       load_stack_value(short i, llvm::Type* type);
	void               store_stack_value(short i, llvm::Value* value);

	llvm::AllocaInst* get_stack_variable(short i, llvm::Type* type);
	llvm::AllocaInst* allocate_stack_variable(short i, llvm::Type* type);

	llvm::Argument* get_argument(std::size_t i);

	JitCompiler&   m_compiler;
	ModuleBuilder& m_module_builder;

	asIScriptFunction& m_script_function;

	llvm::Function*                    m_llvm_function;
	llvm::BasicBlock*                  m_entry_block;
	std::map<short, llvm::AllocaInst*> m_local_variables;
	bool                               m_return_emitted = false;
};

} // namespace asllvm::detail
