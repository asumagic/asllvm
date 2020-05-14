#pragma once

#include <asllvm/config.hpp>
#include <asllvm/detail/builder.hpp>
#include <asllvm/detail/modulemap.hpp>
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

	asCScriptEngine&  engine() { return *m_engine; }
	llvm::orc::LLJIT& jit() { return *m_jit; }
	const JitConfig&  config() const { return m_config; }
	Builder&          builder() { return m_builder; }

	void diagnostic(const std::string& text, asEMsgType message_type = asMSGTYPE_INFORMATION) const;

	void build_modules();

	private:
	std::unique_ptr<llvm::orc::LLJIT> setup_jit();

	void dump_state() const;

	[[no_unique_address]] LibraryInitializer m_llvm_initializer;

	llvm::JITEventListener* m_gdb_listener;
#if LLVM_USE_PERF
	llvm::JITEventListener* m_perf_listener;
#endif
	std::unique_ptr<llvm::orc::LLJIT> m_jit;

	asCScriptEngine* m_engine = nullptr;
	JitConfig        m_config;
	Builder          m_builder;
	ModuleMap        m_module_map;
};

} // namespace asllvm::detail
