#include <angelscript-llvm/jit.hpp>

#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/functionbuilder.hpp>
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

	dump_state();

	if (status == CompileStatus::SUCCESS)
	{
		m_debug_state = {};
		return 0;
	}

	diagnostic(engine, "Function was not JITted. This may cause problems when other function reference this one.\n", asMSGTYPE_WARNING);

	m_debug_state = {};
	return -1;
}

void JitCompiler::ReleaseJITFunction(asJITFunction func)
{

}

JitCompiler::CompileStatus JitCompiler::compile(asIScriptEngine& engine, asIScriptFunction& function, asJITFunction& output)
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


	// TODO: this can be reused instead
	detail::Builder builder;

	detail::ModuleBuilder& module_builder = m_module_map[function.GetModuleName()];

	detail::FunctionBuilder function_builder = module_builder.create_function(function);

	const asDWORD* bytecode_current = bytecode;
	const asDWORD* bytecode_end = bytecode + length;

	while (bytecode_current < bytecode_end)
	{
		const asSBCInfo info = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode_current)];
		const std::size_t instruction_size = asBCTypeSize[info.type];

		switch (info.bc)
		{
		case asBC_JitEntry:
		{
			if (m_config.verbose)
			{
				diagnostic(engine, "Found JIT entry point, patching as valid entry point");
			}

			// If this argument is zero (the default), the script engine will never call into the JIT.
			asBC_PTRARG(bytecode_current) = 1;

			break;
		}

		case asBC_SUSPEND:
		{
			if (m_config.verbose)
			{
				diagnostic(engine, "Found VM suspend, these are unsupported and ignored", asMSGTYPE_WARNING);
			}

			break;
		}

		default:
		{
			diagnostic(engine, fmt::format("Hit unrecognized bytecode instruction {}, aborting", info.name), asMSGTYPE_ERROR);
			return CompileStatus::UNIMPLEMENTED;
		}
		}

		bytecode_current += instruction_size;
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

void JitCompiler::dump_state() const
{
	m_module_map.dump_state();
}

}
