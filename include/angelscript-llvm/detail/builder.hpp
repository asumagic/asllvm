#pragma once

#include <angelscript-llvm/detail/fwd.hpp>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>

namespace asllvm::detail
{
class Builder
{
	public:
	Builder(JitCompiler& compiler);

	llvm::IRBuilder<>& ir_builder() { return m_ir_builder; }

	llvm::Type* script_type_to_llvm_type(int type_id) const;
	bool        is_script_type_64(int type_id) const;

	private:
	JitCompiler&      m_compiler;
	llvm::IRBuilder<> m_ir_builder;
};

} // namespace asllvm::detail
