#include <angelscript-llvm/detail/modulemap.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>

namespace asllvm::detail
{

ModuleBuilder& ModuleMap::operator[](std::string_view name)
{
	// TODO: this string conversion should not be necessary
	if (auto it = m_map.find(std::string(name)); it != m_map.end())
	{
		return it->second;
	}

	const auto [it, success] = m_map.emplace(std::string(name), ModuleBuilder{name});

	return it->second;
}

}
