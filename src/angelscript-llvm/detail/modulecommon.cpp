#include <angelscript-llvm/detail/modulecommon.hpp>

#include <fmt/core.h>

namespace asllvm::detail
{
std::string make_module_name(std::string_view angelscript_module_name)
{
	return fmt::format("asllvm.module.{}", angelscript_module_name);
}

std::string make_function_name(std::string_view angelscript_function_name, std::string_view namespace_name)
{
	auto namespace_fixed = std::string(namespace_name);

	// If no namespace is present, the function symbol will not appear changed
	// If a namespace is present, all the ':' will be replaced with '$' (so we assume '$$' reserved).
	//
	// e.g.
	//     namespace foo { namespace bar { void foobar(); } }
	//
	// will result into the function symbol
	//
	//     foo$$bar$$foobar
	//
	// TODO: make sure all this magic is actually required.
	//       it is in textual LLVM IR; but maybe the builder behaves differently.
	if (!namespace_fixed.empty())
	{
		namespace_fixed += "$$";

		// Do not forget about nested namespaces
		for (char& c : namespace_fixed)
		{
			if (c == ':')
			{
				c = '$';
			}
		}
	}

	// TODO: JIT symbols are common for all modules. this should be per module instead to prevent conflicts.
	return fmt::format("asllvm.localmodule.{}{}", namespace_fixed, angelscript_function_name);
}

std::string make_jit_entry_name(std::string_view angelscript_function_name, std::string_view namespace_name)
{
	return make_function_name(angelscript_function_name, namespace_name) + ".jitentry";
}
} // namespace asllvm::detail
