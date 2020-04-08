#include "common.hpp"
#include <catch2/catch.hpp>

// Note that the 8-bit and 16-bit arithmetic checks are somewhat redundant: operations over these types usually get
// promoted to 32-bit anyway. Checking for this potentially helps detecting bugs related to sign extension and such,
// though.

TEST_CASE("8-bit signed math", "[signedmath8]")
{
	REQUIRE(run_string("int8 a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int8 a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int8 a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int8 a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int8 a = 7, b = 4; print(a % b)") == "3\n");
}

TEST_CASE("16-bit signed math", "[signedmath16]")
{
	REQUIRE(run_string("int16 a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int16 a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int16 a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int16 a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int16 a = 7, b = 4; print(a % b)") == "3\n");
}

TEST_CASE("32-bit signed math", "[signedmath32]")
{
	REQUIRE(run_string("int a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int a = 7, b = 4; print(a % b)") == "3\n");
}

TEST_CASE("64-bit signed math", "[signedmath64]")
{
	REQUIRE(run_string("int64 a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int64 a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int64 a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int64 a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int64 a = 7, b = 4; print(a % b)") == "3\n");
}

TEST_CASE("unsigned overflow logic", "[unsignedmath]")
{
	REQUIRE(run_string("print(uint8(1) + uint8(-2))") == "255\n");
	REQUIRE(run_string("print(uint16(1) + uint16(-2))") == "65535\n");
	REQUIRE(run_string("print(uint32(1) + uint32(-2))") == "4294967295\n");
	REQUIRE(run_string("print(uint64(1) + uint64(-2))") == "18446744073709551615\n");
}
