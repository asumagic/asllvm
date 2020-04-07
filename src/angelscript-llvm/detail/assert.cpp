#include <angelscript-llvm/detail/assert.hpp>

#include <cstdlib>
#include <fmt/core.h>

namespace asllvm::detail
{
void assert_failure_handler(const char* condition, const char* file, long line)
{
	fmt::print(stderr, "{}:{}: asllvm assertion ({}) failed\n", file, line, condition);
	std::abort();
}
} // namespace asllvm::detail
