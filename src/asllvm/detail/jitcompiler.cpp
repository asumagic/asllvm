#include <asllvm/detail/jitcompiler.hpp>

#include <asllvm/detail/assert.hpp>
#include <asllvm/detail/builder.hpp>
#include <asllvm/detail/functionbuilder.hpp>
#include <asllvm/detail/llvmglobals.hpp>
#include <asllvm/detail/modulebuilder.hpp>
#include <asllvm/detail/modulecommon.hpp>
#include <fmt/core.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/Support/TargetSelect.h>

#if !LLVM_USE_PERF
#	pragma message("warning: LLVM was not build with perf support. Disabling perf listener support")
#endif

namespace asllvm::detail
{
LibraryInitializer::LibraryInitializer()
{
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
}

JitCompiler::JitCompiler(JitConfig config) :
	m_llvm_initializer{},
	m_gdb_listener{llvm::JITEventListener::createGDBRegistrationListener()},
#if LLVM_USE_PERF
	m_perf_listener{llvm::JITEventListener::createPerfJITEventListener()},
#endif
	m_jit{setup_jit()},
	m_config{config},
	m_builder{*this},
	m_module_map{*this}
{}

int JitCompiler::jit_compile(asIScriptFunction* function, asJITFunction* output)
{
	asllvm_assert(
		!(m_engine != nullptr && function->GetEngine() != m_engine)
		&& "JIT compiler expects to be used against the same asIScriptEngine during its lifetime");

	m_engine = static_cast<asCScriptEngine*>(function->GetEngine());

	m_module_map[function->GetModule()].append({static_cast<asCScriptFunction*>(function), output});
	return 0;
}

void JitCompiler::jit_free(asJITFunction func)
{
	// TODO
}

void JitCompiler::diagnostic(const std::string& text, asEMsgType message_type) const
{
	asllvm_assert(m_engine != nullptr);

	std::string edited_text = "asllvm: ";
	edited_text += text;

	m_engine->WriteMessage("", 0, 0, message_type, edited_text.c_str());
}

void JitCompiler::build_modules() { m_module_map.build_modules(); }

std::unique_ptr<llvm::orc::LLJIT> JitCompiler::setup_jit()
{
	auto jit = ExitOnError(llvm::orc::LLJITBuilder().create());

	auto& object_linking_layer = static_cast<llvm::orc::RTDyldObjectLinkingLayer&>(jit->getObjLinkingLayer());
	object_linking_layer.setNotifyLoaded([this](
		[[maybe_unused]] llvm::orc::MaterializationResponsibility& a,
		const llvm::object::ObjectFile& b,
		const llvm::RuntimeDyld::LoadedObjectInfo& c) {
		m_gdb_listener->notifyObjectLoaded(
			reinterpret_cast<llvm::JITEventListener::ObjectKey>(&b),
			b,
			c
		);

#if LLVM_USE_PERF
		m_perf_listener->notifyObjectLoaded(a, b, c);
#endif
	});

	object_linking_layer.setProcessAllSections(true);

	return jit;
}

void JitCompiler::dump_state() const { m_module_map.dump_state(); }

} // namespace asllvm::detail
