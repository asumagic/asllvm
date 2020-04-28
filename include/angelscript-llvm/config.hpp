#pragma once

namespace asllvm
{
struct JitConfig
{
	//! \brief Enables LLVM optimizations.
	bool allow_llvm_optimizations : 1;

	//! \brief Allow aggressive floating-point arithmetic at the cost of precision.
	bool allow_fast_math : 1;

	//! \brief Allow optimization of script `final` functions and classes under certain circumstances.
	bool allow_devirtualization : 1;

	//! \brief Whether to emit a lot of diagnostics for debugging.
	bool verbose : 1;

	// bool allow_late_jit_compiles : 1;

	JitConfig() :
		allow_llvm_optimizations{true},
		allow_fast_math{true},
		allow_devirtualization{true},
		verbose{false} /*, allow_late_jit_compiles{true}*/
	{}
};
} // namespace asllvm
