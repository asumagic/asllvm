#pragma once

#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/bytecodeinstruction.hpp>
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
	//! \brief Constructor for FunctionBuilder, usually called by ModuleBuilder::create_function_builder().
	FunctionBuilder(
		JitCompiler&       compiler,
		ModuleBuilder&     module_builder,
		asCScriptFunction& script_function,
		llvm::Function*    llvm_function);

	//! \brief Type used by AngelScript for local variable identifiers.
	//! \details
	//!		- When <= 0, refers to a function parameter. The topmost element is the first passed parameter.
	//!		- When >1, refers to the local function stack.
	//! \note
	//!		Depending on the size of the first parameter, the offset of the first local value might be higher than 1.
	using StackVariableIdentifier = std::int16_t;

	// TODO: exceptions should make more sense than just a std::runtime_error
	//! \brief Generates the LLVM IR for _an entire function_ given its AngelScript bytecode.
	//! \exception Throws a std::runtime_error if code generation has failed.
	llvm::Function* read_bytecode(asDWORD* bytecode, asUINT length);

	//! \brief Generates a function of asJITFunction signature.
	//! \details
	//!		This interacts with the VM registers in order to dispatch the call to the function generated by
	//!		read_bytecode() and return to the VM cleanly.
	llvm::Function* create_wrapper_function();

	private:
	//! \brief Handle a given bytecode instruction for preprocessing.
	//! \details
	//!		The preprocessing stage is currently only used to populate certain internal structures as required by the
	//!		further processing stage.
	//!		In particular, this creates labels that are used for branching instructions.
	//! \see read_bytecode()
	void preprocess_instruction(BytecodeInstruction instruction);

	//! \brief Do the dirty work for the current bytecode instruction.
	//! \details
	//!		This does most of the processing for bytecode instructions, namely, translating a bytecode instruction to
	//!		IR.
	//! \warning
	//!		This requires preprocess_instruction() to have been used beforehand.
	//! \see read_bytecode()
	void process_instruction(BytecodeInstruction instruction);

	//! \brief Get a human-readable disassembly for a given bytecode instruction.
	std::string disassemble(BytecodeInstruction instruction);

	//! \brief Emit stack allocations for structures used locally within the current function.
	//! \see Populates m_locals and m_registers.
	void emit_allocate_local_structures();

	//! \brief Implements a *signed* stack integer extend instruction from type \p source to type \p destination.
	void emit_cast(
		BytecodeInstruction        instruction,
		llvm::Instruction::CastOps op,
		llvm::Type*                source_type,
		llvm::Type*                destination_type);

	//! \brief Implements an stack arithmetic instruction \p op with of type \p type.
	void emit_stack_arithmetic(BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Type* type);

	//! \brief Implements an stack arithmetic instruction (immediate variant) \p op with of type \p type.
	void emit_stack_arithmetic_imm(BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Type* type);

	void emit_increment(llvm::Type* value_type, long by);

	//! \brief Implements an integral comparison instruction.
	//! \details
	//!		Stores compare(lhs, rhs) to the value register, with compare being:
	//!		- `1` if `lhs > rhs`
	//!		- `0` if `lhs == rhs`
	//!		- `-1` if `lhs < rhs`
	void emit_integral_compare(llvm::Value* lhs, llvm::Value* rhs);

	//! \brief Performs the call to a non-script function \p function with parameters read from the stack.
	void emit_system_call(asCScriptFunction& function);

	//! \brief Performs the call to a script function \p function with parameters read from the stack.
	void emit_script_call(asCScriptFunction& function);

	//! \brief Load a LLVM value of type \p type from a stack variable of identifier \p i.
	llvm::Value* load_stack_value(StackVariableIdentifier i, llvm::Type* type);

	//! \brief Store a LLVM value to a stack variable of identifier \p i.
	void store_stack_value(StackVariableIdentifier i, llvm::Value* value);

	//! \brief Get a pointer to a stack value of type \p type and identifier \p i.
	llvm::Value* get_stack_value_pointer(StackVariableIdentifier i, llvm::Type* type);

	//! \brief Get a pointer to a stack value of type i32* and identifier \p i.
	llvm::Value* get_stack_value_pointer(StackVariableIdentifier i);

	void push_stack_value(llvm::Value* value, std::size_t bytes);

	//! \brief Store \p value into the value register.
	//! \details
	//!		Any data can be stored in the value register as long as it is less than 64-bit, otherwise, UB will occur.
	void store_value_register_value(llvm::Value* value);

	//! \brief Load a value of type \p type from the value register.
	//! \see store_value_register_value()
	llvm::Value* load_value_register_value(llvm::Type* type);

	//! \brief
	//!		Get a pointer of type `type->getPointerTo()` to the value register, performing bit casting of the pointer if
	//!		required.
	llvm::Value* get_value_register_pointer(llvm::Type* type);

	//! \brief Insert a basic block at bytecode offset \p offset.
	void insert_label(long offset);

	//! \brief Insert labels for the conditional jump bytecode instruction \p instruction.
	void preprocess_conditional_branch(BytecodeInstruction instruction);

	//! \brief Insert labels for the unconditional jump bytecode instruction \p instruction.
	void preprocess_unconditional_branch(BytecodeInstruction instruction);

	//! \brief
	//!		Determine the llvm::BasicBlock that should be branched to for a successful conditional branch or for an
	//!		unconditional branch of the jump instruction \p instruction.
	llvm::BasicBlock* get_branch_target(BytecodeInstruction instruction);

	//! \brief
	//!		Determine the llvm::BasicBlock that should be branched to for a false conditional branch for the conditional
	//!		jump instruction \p instruction.
	llvm::BasicBlock* get_conditional_fail_branch_target(BytecodeInstruction instruction);

	//! \brief
	//!		Changes the insert point of the IR generator to the new \p block and emit an unconditional branch to
	//!		\p block from the old one if necessary.
	void switch_to_block(llvm::BasicBlock* block);

	long local_storage_size() const;
	long stack_size() const;

	JitCompiler&   m_compiler;
	ModuleBuilder& m_module_builder;

	asCScriptFunction& m_script_function;

	//! \brief Pointer to the LLVM function being generated or that has been generated.
	//! \see read_bytecode(asDWORD*, asUINT)
	llvm::Function* m_llvm_function;

	long m_stack_pointer = 0;

	//! \brief Array of DWORDs used as a local stack for bytecode operations.
	//! \details
	//!		This array can really be thought to be split in two:
	//!		1. Locals, which are refered to relative to the stack frame pointer.
	//!		2. The temporary stack, which never overlaps the locals stack.
	llvm::AllocaInst* m_locals;

	//! \brief
	//!		Value register, used to temporarily store small (<= 64-bit) values (and sometimes for returning data from
	//!		functions).
	llvm::AllocaInst* m_value_register;

	//! \brief
	//!		Object register, a temporary register to hold objects.
	llvm::AllocaInst* m_object_register;

	//! \brief Map from a bytecode offset to a BasicBlock.
	//! \see InstructionContext::offset
	std::map<long, llvm::BasicBlock*> m_jump_map;

	//! \brief Pointer to the RET instruction.
	//! \details AngelScript bytecode functions only use RET once, we can thus assume to have only one exit point.
	asDWORD* m_ret_pointer = nullptr;
};

} // namespace asllvm::detail
