#include <angelscript-llvm/detail/modulecommon.hpp>

#include <fmt/core.h>

namespace asllvm::detail
{
std::string make_module_name(std::string_view angelscript_module_name)
{
	return fmt::format("asllvm.module.{}", angelscript_module_name);
}

std::string make_function_name(asIScriptFunction& function)
{
	return fmt::format("{}.{}", make_module_name(function.GetModuleName()), function.GetDeclaration(true, true, true));
}

std::string make_jit_entry_name(asIScriptFunction& function) { return make_function_name(function) + ".jitentry"; }

std::string make_system_function_name(asIScriptFunction& function)
{
	return fmt::format("asllvm.external.{}", function.GetName());
}

} // namespace asllvm::detail
