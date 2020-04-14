#pragma once

#include <angelscript-llvm/detail/assert.hpp>
#include <angelscript-llvm/jit.hpp>
#include <angelscript.h>
#include <catch2/catch.hpp>
#include <sstream>
#include <string>

#define asllvm_test_check(x)                                                                                           \
	if (!(x))                                                                                                          \
	{                                                                                                                  \
		throw std::runtime_error{"check failed: " #x};                                                                 \
	}                                                                                                                  \
	(void)0

extern std::stringstream out;

struct EngineContext
{
	EngineContext(asllvm::JitConfig config);

	~EngineContext();

	void register_interface();

	asIScriptModule& build(const char* name, const char* script_path);

	void run(asIScriptModule& module, const char* entry_point);

	asIScriptEngine*     engine;
	asllvm::JitInterface jit;
};

std::string run(const char* path, const char* entry = "void main()");
std::string run_string(const char* str);
