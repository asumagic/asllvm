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

	//! \brief
	//!		Dangerous, proceed with caution.
	//!		Allow assuming that `const` system methods do not modify memory visible from JIT'd code other than the
	//!		parameters.
	//! \details
	//!		This is a very aggressive optimization and may cause issues, but if your interface is designed so that this
	//!		is a valid assumption to make, this can yield some benefits.
	bool assume_const_is_pure : 1;

	//! \brief Whether to emit a lot of diagnostics for debugging.
	bool verbose : 1;

	// bool allow_late_jit_compiles : 1;

	JitConfig() :
		allow_llvm_optimizations{true},
		allow_fast_math{true},
		allow_devirtualization{true},
		assume_const_is_pure{false},
		verbose{false} /*, allow_late_jit_compiles{true}*/
	{}
};
} // namespace asllvm
