#pragma once

#include <angelscript-llvm/config.hpp>
#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/modulemap.hpp>
#include <angelscript.h>
#include <string>

namespace asllvm::detail
{
class JitCompiler
{
	public:
	JitCompiler(JitConfig flags = {});

	int  jit_compile(asIScriptFunction* function, asJITFunction* output);
	void jit_free(asJITFunction function);

	Builder&         builder() { return m_builder; }
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

	JitConfig m_config;
	Builder   m_builder;
	ModuleMap m_module_map;
};

} // namespace asllvm::detail
