#include "common.hpp"
#include <catch2/catch.hpp>

TEST_CASE("recursive fibonacci", "[fib]") { REQUIRE(run("fib.as", "void printfib10()") == "55\n"); }
