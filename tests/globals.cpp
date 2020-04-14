#include "common.hpp"

TEST_CASE("globals", "[globals]") { REQUIRE(run("globals.as", "void assign_read()") == "123\n123\n123\n123\n"); }
