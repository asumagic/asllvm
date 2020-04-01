#include <angelscript-llvm/detail/builder.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>

namespace asllvm::detail
{
Builder::Builder(JitCompiler& compiler) : m_compiler{compiler}, m_ir_builder{context} {}
} // namespace asllvm::detail
