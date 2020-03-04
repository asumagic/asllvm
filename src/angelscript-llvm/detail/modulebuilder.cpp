#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>

#include <fmt/core.h>

namespace asllvm::detail
{

ModuleBuilder::ModuleBuilder(const char* name) :
	module{fmt::format("asllvm.module.{}", name), context}
{}

}
