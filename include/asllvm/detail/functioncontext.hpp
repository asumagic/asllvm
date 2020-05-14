#pragma once

#include <asllvm/detail/asinternalheaders.hpp>
#include <asllvm/detail/fwd.hpp>
#include <llvm/IR/Function.h>

namespace asllvm::detail
{
struct FunctionContext
{
	JitCompiler*             compiler;
	ModuleBuilder*           module_builder;
	llvm::Function*          llvm_function;
	const asCScriptFunction* script_function;
};
} // namespace asllvm::detail::codegen
