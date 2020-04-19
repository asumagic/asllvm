#include <angelscript-llvm/detail/jitcompiler.hpp>

#include <angelscript-llvm/detail/assert.hpp>
#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/functionbuilder.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <fmt/core.h>
#include <llvm/Support/TargetSelect.h>

namespace asllvm::detail
{
LibraryInitializer::LibraryInitializer()
{
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
}

JitCompiler::JitCompiler(JitConfig config) :
	m_llvm_initializer{},
	m_jit{ExitOnError(llvm::orc::LLJITBuilder().create())},
	m_config{config},
	m_builder{*this},
	m_module_map{*this}
{}

int JitCompiler::jit_compile(asIScriptFunction* function, asJITFunction* output)
{
	asllvm_assert(
		!(m_engine != nullptr && function->GetEngine() != m_engine)
		&& "JIT compiler expects to be used against the same asIScriptEngine during its lifetime");

	m_engine = function->GetEngine();
	m_module_map[*function->GetModule()].append({static_cast<asCScriptFunction*>(function), output});

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

void JitCompiler::dump_state() const { m_module_map.dump_state(); }

} // namespace asllvm::detail
