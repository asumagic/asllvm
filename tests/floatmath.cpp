#include "common.hpp"
#include <catch2/catch.hpp>

TEST_CASE("32-bit float math", "[floatmath32]")
{
	REQUIRE(run_string("float a = 3.141f; print(''+a);") == "3.141\n");
	REQUIRE(run_string("float a = 1.0f, b = 2.0f; print(''+(a + b));") == "3\n");
	REQUIRE(run_string("float a = 1.0f, b = 2.0f; print(''+(a - b));") == "-1\n");
	REQUIRE(run_string("float a = 5.0f, b = 2.0f; print(''+(a * b));") == "10\n");
	REQUIRE(run_string("float a = 5.0f, b = 2.0f; print(''+(a / b));") == "2.5\n");
	REQUIRE(run_string("float a = 10.0f, b = 6.0f; print(''+(a % b));") == "4\n");
}

TEST_CASE("64-bit float math", "[floatmath32]")
{
	REQUIRE(run_string("double a = 3.141; print(''+a);") == "3.141\n");
	REQUIRE(run_string("double a = 1.0, b = 2.0; print(''+(a + b));") == "3\n");
	REQUIRE(run_string("double a = 1.0, b = 2.0; print(''+(a - b));") == "-1\n");
	REQUIRE(run_string("double a = 5.0, b = 2.0; print(''+(a * b));") == "10\n");
	REQUIRE(run_string("double a = 5.0, b = 2.0; print(''+(a / b));") == "2.5\n");
	REQUIRE(run_string("double a = 10.0, b = 6.0 ; print(''+(a % b));") == "4\n");
}

TEST_CASE("Floating-point to floating-point conversions", "[castfpfp]")
{
	REQUIRE(run_string("double a = 3.141; print(''+float(a))") == "3.141\n");
	REQUIRE(run_string("float a = 3.141; print(''+double(a))") == "3.141\n");
}
