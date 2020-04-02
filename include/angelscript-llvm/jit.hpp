#pragma once

#include <angelscript-llvm/config.hpp>
#include <angelscript-llvm/detail/fwd.hpp>
#include <angelscript.h>
#include <memory>

namespace asllvm
{
class JitInterface final : public asIJITCompiler
{
	public:
	JitInterface(JitConfig flags = {});

	virtual int  CompileFunction(asIScriptFunction* function, asJITFunction* output) override;
	virtual void ReleaseJITFunction(asJITFunction func) override;

	void BuildModules();

	private:
	std::unique_ptr<detail::JitCompiler, void (*)(detail::JitCompiler*)> m_compiler;
};
} // namespace asllvm
