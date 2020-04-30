#pragma once

#include <angelscript-llvm/config.hpp>
#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/modulemap.hpp>
#include <angelscript.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
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
	JitCompiler(JitConfig config = {});

	int  jit_compile(asIScriptFunction* function, asJITFunction* output);
	void jit_free(asJITFunction function);

	asIScriptEngine&  engine() { return *m_engine; }
	llvm::orc::LLJIT& jit() { return *m_jit; }
	const JitConfig&  config() const { return m_config; }
	Builder&          builder() { return m_builder; }

	void diagnostic(const std::string& text, asEMsgType message_type = asMSGTYPE_INFORMATION) const;

	void build_modules();

	private:
	void dump_state() const;

	[[no_unique_address]] LibraryInitializer m_llvm_initializer;
	asIScriptEngine*                         m_engine = nullptr;
	std::unique_ptr<llvm::orc::LLJIT>        m_jit;
	llvm::JITEventListener*                  m_gdb_listener;
#if LLVM_USE_PERF
	llvm::JITEventListener* m_perf_listener;
#endif
	JitConfig m_config;
	Builder   m_builder;
	ModuleMap m_module_map;
};

} // namespace asllvm::detail
