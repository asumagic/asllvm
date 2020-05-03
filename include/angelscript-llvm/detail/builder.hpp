#pragma once

#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/fwd.hpp>
#include <angelscript.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Type.h>
#include <map>

namespace asllvm::detail
{
struct CommonDefinitions
{
	llvm::Type* vm_registers;

	llvm::Type* tvoid;

	llvm::IntegerType *i1, *i8, *i16, *i32, *i64, *iptr;

	llvm::Type *f32, *f64;

	llvm::Type *pvoid, *pi8, *pi16, *pi32, *pi64, *pf32, *pf64;
};

class Builder
{
	public:
	Builder(JitCompiler& compiler);

	llvm::IRBuilder<>& ir() { return m_ir_builder; }
	CommonDefinitions& definitions() { return m_defs; }

	llvm::Type* to_llvm_type(const asCDataType& type) const;

	llvm::legacy::PassManager&         optimizer();
	llvm::LLVMContext&                 context();
	std::unique_ptr<llvm::LLVMContext> extract_old_context();

	private:
	CommonDefinitions setup_common_definitions();

	std::unique_ptr<llvm::LLVMContext> setup_context();
	llvm::legacy::PassManager          setup_pass_manager();

	JitCompiler&                             m_compiler;
	std::unique_ptr<llvm::LLVMContext>       m_context;
	llvm::legacy::PassManager                m_pass_manager;
	llvm::IRBuilder<>                        m_ir_builder;
	CommonDefinitions                        m_defs;
	mutable std::map<int, llvm::StructType*> m_object_types;
};

} // namespace asllvm::detail
