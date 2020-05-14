#include <asllvm/jit.hpp>

#include <asllvm/detail/jitcompiler.hpp>

namespace asllvm
{
static void delete_compiler(detail::JitCompiler* ptr) { delete ptr; }

JitInterface::JitInterface(JitConfig flags) : m_compiler{new detail::JitCompiler{flags}, delete_compiler} {}

int JitInterface::CompileFunction(asIScriptFunction* function, asJITFunction* output)
{
	return m_compiler->jit_compile(function, output);
}

void JitInterface::ReleaseJITFunction(asJITFunction func) { return m_compiler->jit_free(func); }

void JitInterface::BuildModules() { m_compiler->build_modules(); }
} // namespace asllvm
