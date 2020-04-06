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

	m_engine                = function->GetEngine();
	asIScriptEngine& engine = *function->GetEngine();

	CompileStatus status = CompileStatus::ICE;

	try
	{
		status = compile(engine, static_cast<asCScriptFunction&>(*function), *output);
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

		if (*output == nullptr)
		{
			*output = m_config.allow_late_jit_compiles ? late_jit_compile : invalid_late_jit_compile;
		}

		m_debug_state = {};
		return 0;
	}

	diagnostic(
		engine,
		"Function was not JITted. This will likely cause issues if any successfully JIT'd function references this "
		"one.\n",
		asMSGTYPE_WARNING);

	m_debug_state = {};
	return -1;
}

void JitCompiler::jit_free(asJITFunction func)
{
	// TODO
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

void JitCompiler::build_modules() { m_module_map.build_modules(); }

JitCompiler::CompileStatus
JitCompiler::compile(asIScriptEngine& engine, asCScriptFunction& function, asJITFunction& output)
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

	detail::FunctionBuilder function_builder = module_builder.create_function_builder(function);

	function_builder.read_bytecode(bytecode, length);
	function_builder.create_wrapper_function();

	module_builder.add_jit_function(make_function_name(function.GetName(), function.GetNamespace()), &output);

	return CompileStatus::SUCCESS;
}

void JitCompiler::invalid_late_jit_compile([[maybe_unused]] asSVMRegisters* registers, [[maybe_unused]] asPWORD jit_arg)
{
	asllvm_assert(
		false
		&& "JIT module was not built and late JIT compiles were disabled. application forgot a call to BuildModule().");
}

void JitCompiler::late_jit_compile([[maybe_unused]] asSVMRegisters* registers, [[maybe_unused]] asPWORD jit_arg)
{
	reinterpret_cast<JitCompiler*>(jit_arg)->build_modules();

	// We do not know what module we were missing... but we know it has been built, now.
	// Returning without affecting registers (and, in particular, the programPointer) means this JIT entry point will be
	// entered again, but now with the correct pointer.
}

void JitCompiler::dump_state() const { m_module_map.dump_state(); }

} // namespace asllvm::detail
