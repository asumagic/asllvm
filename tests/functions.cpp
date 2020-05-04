#include "common.hpp"

TEST_CASE("simple parameterized function", "[params]") { REQUIRE(run("scripts/functions.as") == "10000\n"); }

TEST_CASE("references to primitives in parameters", "[refparams]")
{
	REQUIRE(run("scripts/refprimitives.as") == "10\n");
}
