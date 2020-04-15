#include "common.hpp"

TEST_CASE("integer comparisons", "[intcmp]")
{
	// int32
	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a != b))") == "true\n");

	// uint32
	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a != b))") == "true\n");

	// int64
	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a != b))") == "true\n");

	// uint64
	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a != b))") == "true\n");
}

TEST_CASE("floating-point comparisons", "[fpcmp]")
{
	// f32
	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	// f64
	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a == b))") == "false\n");
}

TEST_CASE("misc. boolean logic", "[boolmisc]")
{
	// Emits JLowZ
	REQUIRE(run_string("bool a = true; if (a) { print('good'); } else { print('bad'); }") == "good\n");

	// Emits JLowNZ
	REQUIRE(run_string("bool a = true, b = false; if (a || b) { print('good'); } else { print('bad'); }") == "good\n");
}
