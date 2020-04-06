#pragma once

#ifdef NDEBUG
#define asllvm_assert(x) (void)0
#else
#define asllvm_assert(x) \
    if (!(x)) { ::asllvm::detail::assert_failure_handler(#x, __FILE__, __LINE__); } (void)0
#endif

namespace asllvm::detail
{

[[noreturn]] void assert_failure_handler(const char* condition, const char* file, long line);

}
