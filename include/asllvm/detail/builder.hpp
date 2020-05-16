#pragma once

#include <angelscript.h>
#include <asllvm/detail/asinternalheaders.hpp>
#include <asllvm/detail/fwd.hpp>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Type.h>
#include <map>

namespace asllvm::detail
{
struct StandardTypes
{
	//! \brief asSVMRegisters type.
	llvm::Type* vm_registers;

	//! \brief void data type.
	llvm::Type* tvoid;

	llvm::IntegerType *i1, *i8, *i16, *i32, *i64, *iptr;

	llvm::Type* vm_state;

	llvm::Type *f32, *f64;

	// Shorthands for someCommonType->getPointerTo()
	llvm::Type *pvoid, *pi8, *pi16, *pi32, *pi64, *pf32, *pf64, *piptr;
};

class Builder
{
	public:
	Builder(JitCompiler& compiler);

	llvm::IRBuilder<>&            ir() { return m_ir_builder; }
	StandardTypes&                standard_types() { return m_types; }
	llvm::legacy::PassManager&    optimizer() { return m_pass_manager; }
	llvm::orc::ThreadSafeContext& llvm_context() { return m_context; }

	llvm::Type* to_llvm_type(const asCDataType& type) const;

	private:
	StandardTypes setup_standard_types();

	std::unique_ptr<llvm::LLVMContext> setup_context();
	llvm::legacy::PassManager          setup_pass_manager();

	JitCompiler& m_compiler;

	llvm::orc::ThreadSafeContext m_context;
	llvm::legacy::PassManager    m_pass_manager;
	llvm::IRBuilder<>            m_ir_builder;

	StandardTypes m_types;

	mutable std::map<int, llvm::StructType*> m_object_types;
};

} // namespace asllvm::detail
