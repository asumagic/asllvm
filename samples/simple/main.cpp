#include <angelscript-llvm/jit.hpp>
#include <angelscript.h>
#include <scriptbuilder/scriptbuilder.h>
#include <scriptstdstring/scriptstdstring.h>

#include <iostream>
#include <string>
#include <utility>

constexpr int fail_code = 1;

void check(int return_value, const char* diagnostic = nullptr)
{
	if (return_value < 0)
	{
		if (diagnostic != nullptr)
		{
			std::cerr << "Assertion failed: " << diagnostic;
		}
		else
		{
			std::cerr << "Assertion failed";
		}

		std::cerr << " (return code = " << return_value << ")\n";
		std::exit(fail_code);
	}
}

void message_callback(const asSMessageInfo* info, [[maybe_unused]] void* param)
{
	const char* message_type = nullptr;

	switch (info->type)
	{
	case asMSGTYPE_INFORMATION:
	{
		message_type = "INFO";
		break;
	}

	case asMSGTYPE_WARNING:
	{
		message_type = "WARN";
		break;
	}

	default:
	{
		message_type = "ERR ";
		break;
	}
	}

	std::cerr << info->section << ':' << info->row << ':' << info->col << ": " << message_type << ": " << info->message
			  << '\n';
}

void print(const std::string& message) { std::cout << message << '\n'; }

void setup_jit(asIScriptEngine* engine, asllvm::JitInterface& jit)
{
	engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, true);

	check(engine->SetJITCompiler(&jit), "Setting JIT compiler");
}

void register_interface(asIScriptEngine* engine)
{
	RegisterStdString(engine);

	check(engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(print), asCALL_CDECL));
	check(engine->SetMessageCallback(asFUNCTION(message_callback), nullptr, asCALL_CDECL));
}

void build(asIScriptEngine* engine, const char* module_name, const char* file_path)
{
	CScriptBuilder builder;
	check(builder.StartNewModule(engine, module_name), "Setting up new module");
	check(builder.AddSectionFromFile(file_path), "Compiling script");
	check(builder.BuildModule(), "Building module");
}

void execute(asIScriptEngine* engine, asIScriptFunction* function)
{
	asIScriptContext* context = engine->CreateContext();

	check(context->Prepare(function), "Preparing context for function");

	if (int r = context->Execute(); r != asEXECUTION_FINISHED)
	{
		std::cerr << "Execution did not finish\n";

		if (r == asEXECUTION_EXCEPTION)
		{
			std::cerr << "Exception occured: " << context->GetExceptionString() << '\n';
		}
	}

	context->Release();
}

int main()
{
	asIScriptEngine* engine = asCreateScriptEngine();

	if (engine == nullptr)
	{
		std::cerr << "Failed to create engine\n";
		return fail_code;
	}

	asllvm::JitConfig config;
	config.verbose = true;

	asllvm::JitInterface jit{config};
	setup_jit(engine, jit);

	register_interface(engine);

	build(engine, "build", "script.as");

	{
		asIScriptModule*   module   = engine->GetModule("build");
		asIScriptFunction* function = module->GetFunctionByDecl("void main()");

		if (function == nullptr)
		{
			std::cerr << "Missing entry point 'void main()'\n";
			return fail_code;
		}

		execute(engine, function);
	}

	engine->Release();
}
