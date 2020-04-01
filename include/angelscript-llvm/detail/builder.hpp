#pragma once

#include <angelscript-llvm/detail/fwd.hpp>
#include <llvm/IR/IRBuilder.h>

namespace asllvm::detail
{
class Builder
{
	public:
	Builder(JitCompiler& compiler);

	llvm::IRBuilder<>& ir_builder() { return m_ir_builder; }

	private:
	JitCompiler& m_compiler;

	llvm::IRBuilder<> m_ir_builder;
};

} // namespace asllvm::detail
