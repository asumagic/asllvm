#include "common.hpp"

TEST_CASE("recursive fibonacci", "[fib]") { REQUIRE(run("scripts/fib.as", "void printfib10()") == "55\n"); }
