#include <catch2/catch.hpp>

#include "common.hpp"

TEST_CASE("signed add logic", "[signedmath]")
{
	REQUIRE(run_string("print(int8(1) + int8(-2))") == "-1\n");
	REQUIRE(run_string("print(int16(1) + int16(-2))") == "-1\n");
	REQUIRE(run_string("print(int32(1) + int32(-2))") == "-1\n");
	REQUIRE(run_string("print(int64(1) + int64(-2))") == "-1\n");
}

TEST_CASE("unsigned overflow logic", "[unsignedmath]")
{
	REQUIRE(run_string("print(uint8(1) + uint8(-2))") == "255\n");
	REQUIRE(run_string("print(uint16(1) + uint16(-2))") == "65535\n");
	REQUIRE(run_string("print(uint32(1) + uint32(-2))") == "4294967295\n");
	REQUIRE(run_string("print(uint64(1) + uint64(-2))") == "18446744073709551615\n");
}
