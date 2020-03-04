#include <angelscript-llvm/detail/modulecommon.hpp>

#include <fmt/core.h>

namespace asllvm::detail
{

std::string make_module_name(std::string_view angelscript_module_name)
{
	return fmt::format("asllvm.module.{}", angelscript_module_name);
}

}
