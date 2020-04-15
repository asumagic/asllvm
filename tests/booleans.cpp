#include "common.hpp"

TEST_CASE("integer comparisons", "[intcmp]")
{
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
}
