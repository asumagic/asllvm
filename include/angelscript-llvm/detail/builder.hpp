#pragma once

#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/fwd.hpp>
#include <angelscript.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Type.h>

namespace asllvm::detail
{
struct CommonDefinitions
{
	llvm::Type* vm_registers;

	llvm::Type *tvoid, *i1, *i8, *i16, *i32, *i64;
	llvm::Type *pvoid, *pi8, *pi16, *pi32, *pi64;
};

class Builder
{
	public:
	Builder(JitCompiler& compiler);

	llvm::IRBuilder<>& ir() { return m_ir_builder; }
	CommonDefinitions& definitions() { return m_common_definitions; }

	llvm::Type* to_llvm_type(asCDataType& type) const;
	bool        is_script_type_64(asCDataType& type) const;
	std::size_t get_script_type_dword_size(asCDataType& type) const;

	llvm::legacy::PassManager&         optimizer();
	llvm::LLVMContext&                 context();
	std::unique_ptr<llvm::LLVMContext> extract_old_context();

	private:
	CommonDefinitions setup_common_definitions();

	std::unique_ptr<llvm::LLVMContext> setup_context();
	llvm::legacy::PassManager          setup_pass_manager();

	std::unique_ptr<llvm::LLVMContext> m_context;
	JitCompiler&                       m_compiler;
	llvm::legacy::PassManager          m_pass_manager;
	llvm::IRBuilder<>                  m_ir_builder;
	CommonDefinitions                  m_common_definitions;
};

} // namespace asllvm::detail
