#include "common.hpp"
#include <catch2/catch.hpp>

TEST_CASE("string handling", "[str]")
{
	REQUIRE(run("stringmanip.as", "void string_ref()") == "hello\n");
	REQUIRE(run("stringmanip.as", "void string_concat()") == "hello world\n");
	REQUIRE(run("stringmanip.as", "void string_concat2()") == "hello world\n");
	REQUIRE(run("stringmanip.as", "void string_manylocals_concat()") == "hello world\n");
	REQUIRE(run("stringmanip.as", "void string_function_reference()") == "hello\n");
	// REQUIRE(run("stringmanip.as", "void string_function_value()") == "hello\n");
	REQUIRE(run_string(R"(print(parseInt("ABCD", 16)))") == "43981\n");
}

TEST_CASE("user classes", "[userclass]") { REQUIRE(run("userclasses.as", "void test()") == "hello\n"); }
