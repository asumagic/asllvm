#pragma once

#include <angelscript-llvm/config.hpp>
#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/modulemap.hpp>
#include <angelscript.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <string>

namespace asllvm::detail
{
struct LibraryInitializer
{
	LibraryInitializer();
};

class JitCompiler
{
	public:
	JitCompiler(JitConfig flags = {});

	int  jit_compile(asIScriptFunction* function, asJITFunction* output);
	void jit_free(asJITFunction function);

	asIScriptEngine&  engine() { return *m_engine; }
	llvm::orc::LLJIT& jit() { return *m_jit; }
	const JitConfig&  config() const { return m_config; }
	Builder&          builder() { return m_builder; }

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

	static void late_jit_compile(asSVMRegisters* registers, asPWORD jit_arg);
	static void invalid_late_jit_compile(asSVMRegisters* registers, asPWORD jit_arg);

	void dump_state() const;

	[[no_unique_address]] LibraryInitializer m_llvm_initializer;
	asIScriptEngine*                         m_engine = nullptr;
	std::unique_ptr<llvm::orc::LLJIT>        m_jit;
	JitConfig                                m_config;
	Builder                                  m_builder;
	ModuleMap                                m_module_map;
};

} // namespace asllvm::detail
