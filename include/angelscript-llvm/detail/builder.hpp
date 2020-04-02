#pragma once

#include <angelscript-llvm/detail/fwd.hpp>
#include <llvm/IR/IRBuilder.h>
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

	private:
	CommonDefinitions build_common_definitions();

	JitCompiler&      m_compiler;
	llvm::IRBuilder<> m_ir_builder;
	CommonDefinitions m_common_definitions;
};

} // namespace asllvm::detail
