#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>

#include <angelscript-llvm/detail/modulecommon.hpp>

#include <fmt/core.h>

namespace asllvm::detail
{

ModuleBuilder::ModuleBuilder(std::string_view angelscript_module_name) :
	module{std::make_unique<llvm::Module>(make_module_name(angelscript_module_name), context)}
{}

}
