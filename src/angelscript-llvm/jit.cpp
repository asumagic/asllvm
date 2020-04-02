#include <angelscript-llvm/jit.hpp>

#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/functionbuilder.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <fmt/core.h>

namespace asllvm
{
JitCompiler::JitCompiler(JitConfig config) : m_config{config}, m_builder{*this}, m_module_map{*this} {}

int JitCompiler::CompileFunction(asIScriptFunction* function, asJITFunction* output)
{
	asIScriptEngine& engine = *function->GetEngine();

	CompileStatus status = CompileStatus::ICE;

	try
	{
		status = compile(engine, *function, *output);
	}
	catch (std::runtime_error& error)
	{
		diagnostic(engine, fmt::format("Failed to compile module: {}\n", error.what()), asMSGTYPE_ERROR);
	}

	if (status == CompileStatus::SUCCESS)
	{
		if (m_config.verbose)
		{
			diagnostic(engine, "Function JITted successfully.\n", asMSGTYPE_INFORMATION);
		}

		m_debug_state = {};
		return 0;
	}

	diagnostic(
		engine,
		"Function was not JITted. This may cause problems when other function reference this one.\n",
		asMSGTYPE_WARNING);

	m_debug_state = {};
	return -1;
}

void JitCompiler::ReleaseJITFunction(asJITFunction func) {}

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

JitCompiler::CompileStatus
JitCompiler::compile(asIScriptEngine& engine, asIScriptFunction& function, asJITFunction& output)
{
	m_debug_state.compiling_function = &function;

	if (m_config.verbose)
	{
		diagnostic(engine, fmt::format("JIT compiling {}", function.GetDeclaration(true, true, true)));
	}

	asUINT   length;
	asDWORD* bytecode = function.GetByteCode(&length);

	if (bytecode == nullptr)
	{
		diagnostic(engine, "Null bytecode passed by engine", asMSGTYPE_WARNING);
		return CompileStatus::NULL_BYTECODE;
	}

	detail::ModuleBuilder& module_builder = m_module_map[function.GetModuleName()];

	try // yolo
	{
		detail::FunctionBuilder function_builder = module_builder.create_function(function);
		function_builder.read_bytecode(bytecode, length);
		function_builder.create_wrapper_function();
	}
	catch (std::runtime_error& e)
	{
		module_builder.dump_state();
		throw;
	}

	module_builder.dump_state();

	return CompileStatus::SUCCESS;
}

void JitCompiler::dump_state() const { m_module_map.dump_state(); }

} // namespace asllvm
