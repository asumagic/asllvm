#include <angelscript-llvm/detail/modulemap.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <fmt/core.h>

namespace asllvm::detail
{

ModuleMap::ModuleMap(Builder& builder) : m_builder{&builder} {}

ModuleBuilder& ModuleMap::operator[](std::string_view name)
{
	// TODO: this string conversion should not be necessary
	if (auto it = m_map.find(std::string(name)); it != m_map.end())
	{
		return it->second;
	}

	const auto [it, success] = m_map.emplace(std::string(name), ModuleBuilder{*m_builder, name});

	return it->second;
}

void ModuleMap::dump_state() const
{
	for (const auto& [name, module_builder] : m_map)
	{
		fmt::print(stderr, "\nModule '{}':\n", name);
		module_builder.dump_state();
	}
}

}
