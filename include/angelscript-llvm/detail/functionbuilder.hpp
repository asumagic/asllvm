#pragma once

#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/bytecodeinstruction.hpp>
#include <angelscript-llvm/detail/codegen/stackframe.hpp>
#include <angelscript-llvm/detail/fwd.hpp>
#include <angelscript.h>
#include <functional>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <map>
#include <string_view>
#include <vector>

namespace asllvm::detail
{
class FunctionBuilder
{
	struct PreprocessContext
	{
		long current_switch_offset;
		bool handling_jump_table = false;
	};

	enum class GeneratedFunctionType
	{
		Implementation,
		VmEntryThunk
	};

	struct VmEntryCallContext
	{
		llvm::Value *vm_frame_pointer = nullptr, *value_register = nullptr, *object_register = nullptr;
	};

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
	llvm::Function* translate_bytecode(asDWORD* bytecode, asUINT length);

	//! \brief Generates a function of asJITFunction signature.
	//! \details
	//!		This interacts with the VM registers in order to dispatch the call to the function generated by
	//!		read_bytecode() and return to the VM cleanly.
	llvm::Function* create_vm_entry_thunk();

	private:
	//! \brief Handle a given bytecode instruction for preprocessing.
	//! \details
	//!		The preprocessing stage is currently only used to populate certain internal structures as required by the
	//!		further processing stage.
	//!		In particular, this creates labels that are used for branching instructions.
	//! \see read_bytecode()
	void preprocess_instruction(BytecodeInstruction instruction, PreprocessContext& ctx);

	//! \brief Do the dirty work for the current bytecode instruction.
	//! \details
	//!		This does most of the processing for bytecode instructions, namely, translating a bytecode instruction to
	//!		IR.
	//! \warning
	//!		This requires preprocess_instruction() to have been used beforehand.
	//! \see read_bytecode()
	void translate_instruction(BytecodeInstruction instruction);

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
	//! \details LHS and RHS will be determined from the stack.
	void emit_binop(BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Type* type);

	//! \brief Implements an stack arithmetic instruction \p op with of type \p type.
	//! \details LHS will be determined from the stack.
	void emit_binop(BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Value* rhs);

	//! \brief Implements an stack arithmetic instruction \p op with of type \p type.
	void
	emit_binop(BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Value* lhs, llvm::Value* rhs);

	void emit_neg(BytecodeInstruction instruction, llvm::Type* type);
	void emit_bit_not(BytecodeInstruction instruction, llvm::Type* type);
	void emit_condition(llvm::CmpInst::Predicate pred);

	void emit_increment(llvm::Type* value_type, long by);

	//! \brief Implements an integral comparison instruction.
	//! \details
	//!		Stores compare(lhs, rhs) to the value register, with compare being:
	//!		- `1` if `lhs > rhs`
	//!		- `0` if `lhs == rhs`
	//!		- `-1` if `lhs < rhs`
	void emit_compare(llvm::Value* lhs, llvm::Value* rhs, bool is_signed = true);

	//! \brief Performs the call to a non-script function \p function with parameters read from the stack.
	void emit_system_call(const asCScriptFunction& function);

	//! \brief Performs the call to the \p callee script function reading from the currently translated function.
	//! \returns The amount of DWORDs read.
	std::size_t emit_script_call(const asCScriptFunction& callee);

	// TODO: seems like this could be moved elsewhere? move vmentry codegen somewhere else?
	//! \brief Performs the call to the \p callee script function for a vm entry.
	//! \returns The amount of DWORDs read.
	std::size_t emit_script_call(const asCScriptFunction& callee, VmEntryCallContext ctx);

	//! \brief Performs the call to a script or system function \p function.
	void emit_call(const asCScriptFunction& function);

	//! \brief Match for asCScriptEngine::CallObjectMethod, for lack of a better name.
	void emit_object_method_call(const asCScriptFunction& function, llvm::Value* object);

	void emit_conditional_branch(BytecodeInstruction ins, llvm::CmpInst::Predicate predicate);

	llvm::Value* resolve_virtual_script_function(llvm::Value* script_object, const asCScriptFunction& callee);

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

	void create_function_debug_info(llvm::Function* function, GeneratedFunctionType type);

	llvm::Value* load_global(asPWORD address, llvm::Type* type);

	codegen::FunctionContext m_context;

	//! \brief
	//!		Value register, used to temporarily store small (<= 64-bit) values (and sometimes for returning data from
	//!		functions).
	llvm::AllocaInst* m_value_register;

	//! \brief
	//!		Object register, a temporary register to hold objects.
	llvm::AllocaInst* m_object_register;

	codegen::StackFrame m_stack;

	//! \brief Map from a bytecode offset to a BasicBlock.
	//! \see InstructionContext::offset
	std::map<long, llvm::BasicBlock*> m_jump_map;

	//! \brief Map from the bytecode offset of a asBC_JMPP opcode (used for jump tables) to the offsets of the targets.
	//! \see InstructionContext::offset
	std::map<long, std::vector<llvm::BasicBlock*>> m_switch_map;

	//! \brief Pointer to the RET instruction.
	//! \details AngelScript bytecode functions only use RET once, we can thus assume to have only one exit point.
	asDWORD* m_ret_pointer = nullptr;
};

} // namespace asllvm::detail
