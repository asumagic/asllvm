#include <angelscript-llvm/jit.hpp>
#include <fmt/core.h>

namespace asllvm
{

JitCompiler::JitCompiler(JitConfig config) :
	m_config{config}
{}

int JitCompiler::CompileFunction(asIScriptFunction* function, asJITFunction* output)
{
	asIScriptEngine& engine = *function->GetEngine();

	CompileStatus status = compile(engine, *function, *output);

	if (status == CompileStatus::SUCCESS)
	{
		m_debug_state = {};
		return 0;
	}

	diagnostic(engine, "Function was not JITted", asMSGTYPE_WARNING);

	m_debug_state = {};
	return -1;
}

void JitCompiler::ReleaseJITFunction(asJITFunction func)
{

}

JitCompiler::CompileStatus JitCompiler::compile(asIScriptEngine& engine, asIScriptFunction& function, asJITFunction& output) const
{
	m_debug_state.compiling_function = &function;

	if (m_config.verbose)
	{
		diagnostic(engine, fmt::format("JIT compiling {}", function.GetDeclaration(true, true, true)));
	}

	asUINT length;
	const asDWORD* bytecode = function.GetByteCode(&length);

	if (bytecode == nullptr)
	{
		diagnostic(engine, "Null bytecode passed by engine", asMSGTYPE_WARNING);
		return CompileStatus::NULL_BYTECODE;
	}


	return CompileStatus::UNIMPLEMENTED;
}

void JitCompiler::diagnostic(asIScriptEngine& engine, const std::string& text, asEMsgType message_type) const
{
	const char* section = "???";

	if (m_debug_state.compiling_function != nullptr)
	{
		section = m_debug_state.compiling_function->GetModuleName();
	}

	std::string edited_text = "asllvm: ";
	edited_text += text;

	engine.WriteMessage(section, 0, 0, message_type, edited_text.c_str());
}

}
