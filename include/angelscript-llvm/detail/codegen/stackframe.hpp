#pragma once

#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/codegen/context.hpp>
#include <llvm/IR/Instructions.h>
#include <map>

namespace asllvm::detail::codegen
{
struct Parameter
{
	//! \brief Index of the argument in the argument list of the translated function.
	std::size_t argument_index = 0;

	//! \brief
	//!		Local alloca variable where the LLVM argument is stored, which is useful as we often store a pointer to
	//!		a parameter or modify it.
	llvm::AllocaInst* local_alloca = nullptr;

	//! \brief AngelScript type id for this parameter.
	int type_id = 0;

	//! \brief Name that shows up in DWARF debug info.
	const char* debug_name = "";
};

class StackFrame
{
	public:
	//! \brief A stack offset as defined within the AngelScript bytecode.
	//! \details
	//!		A stack offset can refer to different areas:
	//!		- `offset <= 0`: Parameters. See m_parameters.
	//!		- `0 < offset <= variable_space()`: Local variables. See m_storage.
	//!		- `variable_space() < offset <= total_space()`: Temporary stack storage.
	//!		  Can be `== variable_space` when full.
	using AsStackOffset = long;

	StackFrame(Context context);

	void setup();
	void finalize();

	long variable_space() const;
	long stack_space() const;
	long total_space() const;

	AsStackOffset current_stack_pointer() const;
	void          ugly_hack_stack_pointer_within_bounds();
	bool          empty_stack() const;

	void check_stack_pointer_bounds();

	void         push(llvm::Value* value, std::size_t dwords);
	void         pop(std::size_t dwords);
	llvm::Value* pop(std::size_t dwords, llvm::Type* type);
	llvm::Value* top(llvm::Type* type);

	llvm::Value* load(AsStackOffset offset, llvm::Type* type);
	void         store(AsStackOffset offset, llvm::Value* value);

	llvm::Value* pointer_to(AsStackOffset offset, llvm::Type* pointee_type);
	llvm::Value* pointer_to(AsStackOffset offset);

	llvm::AllocaInst* storage_alloca();

	private:
	void allocate_parameter_storage();

	Context m_context;

	//! \brief Array of DWORDs used as local storage for bytecode operations.
	//! \details
	//!		This array can really be thought to be split in two:
	//!		1. Locals, which are addressed relative to the frame pointer (loaded at a fixed index)
	//!		2. The temporary stack, which is used, among other things, to push parameters to pass to functions.
	//! \see AsStackOffset
	llvm::AllocaInst* m_storage;

	//! \brief Mapping from offsets within this stack frame to parameters.
	//! \see AsStackOffset
	std::map<AsStackOffset, Parameter> m_parameters;

	//! \brief Current stack pointer, relevant during IR generation.
	//! \see asSVMRegisters::stackPointer
	AsStackOffset m_stack_pointer;
};
} // namespace asllvm::detail::codegen
