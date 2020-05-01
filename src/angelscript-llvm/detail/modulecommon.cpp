#include <angelscript-llvm/detail/modulecommon.hpp>

#include <fmt/core.h>

namespace asllvm::detail
{
std::string make_module_name(asIScriptModule& module) { return fmt::format("asllvm.module.{}", module.GetName()); }

std::string make_function_name(asIScriptFunction& function)
{
	return fmt::format("{}.{}", make_module_name(*function.GetModule()), function.GetDeclaration(true, true, false));
}

std::string make_jit_entry_name(asIScriptFunction& function) { return make_function_name(function) + ".jitentry"; }

std::string make_system_function_name(asIScriptFunction& function)
{
	return fmt::format("asllvm.external.{}", function.GetDeclaration(true, true, false));
}

std::string make_debug_name(asIScriptFunction& function)
{
	std::string name;

	if (const char* ns = function.GetNamespace(); ns != nullptr)
	{
		name += ns;
		name += "::";
	}

	if (const asITypeInfo* info = function.GetObjectType(); info != nullptr)
	{
		name += info->GetName();
		name += "::";
	}

	name += function.GetName();

	return name;
}

} // namespace asllvm::detail
