#pragma once

#define CATCH_CONFIG_ENABLE_BENCHMARKING

#include <asllvm/detail/assert.hpp>
#include <asllvm/jit.hpp>
#include <angelscript.h>
#include <catch2/catch.hpp>
#include <sstream>
#include <string>

#define TEST_REQUIRE(name, tag, cond)                                                                                  \
	TEST_CASE(name, tag) { REQUIRE(cond); }

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

	void prepare_execution();

	void run(asIScriptModule& module, const char* entry_point);

	asIScriptEngine*     engine;
	asllvm::JitInterface jit;
};

asllvm::JitConfig default_jit_config();

std::string run(const char* path, const char* entry = "void main()");
std::string run(EngineContext& context, const char* path, const char* entry = "void main()");
std::string run_string(const char* str);
std::string run_string(EngineContext& context, const char* str);
