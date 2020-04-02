#pragma once

namespace asllvm
{
struct JitConfig
{
	bool allow_llvm_optimizations : 1;
	bool verbose : 1;
	bool allow_late_jit_compiles : 1;

	JitConfig() : allow_llvm_optimizations{true}, verbose{false}, allow_late_jit_compiles{true} {}
};
} // namespace asllvm
