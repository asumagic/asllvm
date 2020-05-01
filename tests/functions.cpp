#include "common.hpp"

TEST_CASE("simple parameterized function", "[params]")
{
    REQUIRE(run("scripts/functions.as") == "10000\n");
}
