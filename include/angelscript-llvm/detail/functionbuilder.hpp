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

	//! \brief Type used by AngelScript for local variable identifiers.
	//! \details
	//!		- When <= 0, refers to a function parameter. 0 refers to the 1st parameter, -1 to the 2nd, and so on.
	//!		- When >1, refers to a local variable or temporary stack slot.
	//! \note
	//!		Since a stack slot is 32-bit in AngelScript, 64-bit values take up two slots.
	//!		We do not really care for local variables, and just emit (at most) one extra stack value that may not get
	//!		used. We, however, _do_ care for arguments.
	using StackVariableIdentifier = std::int16_t;

	// TODO: exceptions should make more sense than just a std::runtime_error
	//! \brief Generates the LLVM IR for _an entire function_ given its AngelScript bytecode.
	//! \exception Throws a std::runtime_error if code generation has failed.
	void read_bytecode(asDWORD* bytecode, asUINT length);

	private:
	void preprocess_instruction(asDWORD* bytecode);
	void read_instruction(asDWORD* bytecode);

	//! \brief Load a LLVM value of type \p type from a local variable or parameter of identifier \p! i.
	llvm::Value* load_stack_value(StackVariableIdentifier i, llvm::Type* type);

	//! \brief Store a LLVM value to a local variable of identifier \p i.
	void store_stack_value(StackVariableIdentifier i, llvm::Value* value);

	//! \brief Get the value of a local variable or parameter of type \p type and identifier \p i.
	llvm::Value* get_stack_variable(StackVariableIdentifier i, llvm::Type* type);

	//! \brief Allocate enough stack variables so that the stack variable identifier \p count becomes valid.
	//! \details
	//!		When `count < 0`, no operation occurs.
	//!
	//!		For each stack variable that requires to be allocated, an `alloca` of type `i64` is performed in the LLVM
	//!		IR. The reason is that stack variables can get reused with different types, and `i64` is as large as stack
	//!		values can go in the AngelScript bytecode.
	//!		A bit cast is performed on the allocated pointer by load_stack_value() and store_stack_value().
	//!
	//! \warning
	//!		This should be called before emitting any other instructions, i.e. this should be called by
	//!		preprocess_instruction().
	//!		The reason being that if the assignment to a stack value occurs within a branch, the `alloca` will
	//!		incorrectly be performed within the branch. This is a problem if the same stack slot is reused elsewhere.
	void reserve_variable(StackVariableIdentifier count);

	JitCompiler&   m_compiler;
	ModuleBuilder& m_module_builder;

	asIScriptFunction& m_script_function;

	llvm::Function*               m_llvm_function;
	llvm::BasicBlock*             m_entry_block;
	bool                          m_return_emitted = false;
	std::map<short, llvm::Value*> m_variables;
	short                         m_highest_allocated = 0;
};

} // namespace asllvm::detail
