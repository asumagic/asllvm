#include <angelscript-llvm/detail/ashelper.hpp>

#include <string>

asCScriptFunction* asllvm::detail::get_nonvirtual_match(asCScriptFunction& script_function)
{
	// TODO: find a way to make it less garbage
	const auto method_count = script_function.objectType->GetMethodCount();

	const std::string declaration = script_function.GetDeclaration(true, true, false);

	for (std::size_t i = 0; i < method_count; ++i)
	{
		auto& potential_match
			= *static_cast<asCScriptFunction*>(script_function.objectType->GetMethodByIndex(i, false));

		if (declaration == potential_match.GetDeclaration(true, true, false))
		{
			return &potential_match;
		}
	}

	return nullptr;
}
