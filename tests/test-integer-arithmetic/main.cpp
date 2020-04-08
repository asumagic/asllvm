#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <angelscript-llvm/tests/common.hpp>

TEST_CASE("Basic integral math", "[intmath]")
{
	REQUIRE(asllvm::tests::run("script.as", "void signed_add_tests()") == "-1\n-1\n-1\n-1\n");
	REQUIRE(asllvm::tests::run("script.as", "void unsigned_add_tests()") == "255\n65535\n3000000\n3000000000000\n");
}
