#include <asllvm/detail/modulecommon.hpp>

#include <asllvm/detail/assert.hpp>
#include <fmt/core.h>

namespace asllvm::detail
{
std::string make_module_name(const asIScriptModule* module)
{
	if (module == nullptr)
	{
		return "asllvm.shared";
	}

	return fmt::format("asllvm.module.{}", module->GetName());
}

std::string make_function_name(const asIScriptFunction& function)
{
	return fmt::format("{}.{}", make_module_name(function.GetModule()), function.GetDeclaration(true, true, false));
}

std::string make_vm_entry_thunk_name(const asIScriptFunction& function)
{
	return make_function_name(function) + ".vmthunk";
}

std::string make_system_function_name(const asIScriptFunction& function)
{
	return fmt::format("asllvm.external.{}", function.GetDeclaration(true, true, false));
}

std::string make_debug_name(const asIScriptFunction& function)
{
	std::string name;

	asllvm_assert(function.GetNamespace() != nullptr);

	// We do this regardless of whether the namespace is ""
	name += function.GetNamespace();
	name += "::";

	if (const asITypeInfo* info = function.GetObjectType(); info != nullptr)
	{
		name += info->GetName();
		name += "::";
	}

	name += function.GetName();

	return name;
}
} // namespace asllvm::detail
