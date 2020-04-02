#pragma once

#include <angelscript-llvm/config.hpp>
#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/modulemap.hpp>
#include <angelscript.h>

#include <string>

namespace asllvm
{
class JitCompiler : public asIJITCompiler
{
	public:
	JitCompiler(JitConfig flags = {});
	virtual ~JitCompiler() = default;

	virtual int  CompileFunction(asIScriptFunction* function, asJITFunction* output) override;
	virtual void ReleaseJITFunction(asJITFunction func) override;

	detail::Builder& builder() { return m_builder; }
	const JitConfig& config() const { return m_config; }

	void diagnostic(
		asIScriptEngine& engine, const std::string& message, asEMsgType message_type = asMSGTYPE_INFORMATION) const;

	void build_modules();

	private:
	mutable struct
	{
		asIScriptFunction* compiling_function = nullptr;
	} m_debug_state;

	enum class CompileStatus
	{
		SUCCESS,

		NULL_BYTECODE,

		UNIMPLEMENTED,

		ICE
	};

	CompileStatus compile(asIScriptEngine& engine, asIScriptFunction& function, asJITFunction& output);

	void dump_state() const;

	JitConfig         m_config;
	detail::Builder   m_builder;
	detail::ModuleMap m_module_map;
};
} // namespace asllvm
