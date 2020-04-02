#pragma once

#include <angelscript-llvm/detail/fwd.hpp>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Type.h>

namespace asllvm::detail
{
struct CommonDefinitions
{
	llvm::Type* vm_registers;
};

class Builder
{
	public:
	Builder(JitCompiler& compiler);

	llvm::IRBuilder<>& ir_builder() { return m_ir_builder; }
	CommonDefinitions& definitions() { return m_common_definitions; }

	llvm::Type* script_type_to_llvm_type(int type_id) const;
	bool        is_script_type_64(int type_id) const;

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
