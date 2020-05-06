#pragma once

#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/fwd.hpp>
#include <llvm/IR/Function.h>

namespace asllvm::detail::codegen
{
struct Context
{
	Builder* builder;
	llvm::Function* llvm_function;
	const asCScriptFunction* script_function;

	asCScriptEngine& engine()
	{
		return *static_cast<asCScriptEngine*>(script_function->GetEngine());
	}
};
}
