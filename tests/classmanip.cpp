#include "common.hpp"

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

TEST_CASE("user classes", "[userclass]")
{
	REQUIRE(run("userclasses.as", "void test()") == "hello\n");
	REQUIRE(run("userclasses.as", "void method_test()") == "hello\n123\n456\n789\n");
	REQUIRE(run("userclasses.as", "void method_field_test()") == "hello\n10\n20\n30\n40\n50\n60\n70\n80\n90\n100\n");
	// REQUIRE(run("userclasses.as", "void handle_test()") == "hello\n123\n456\n789\n");
}
