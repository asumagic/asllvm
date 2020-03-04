#pragma once

#include <angelscript-llvm/detail/modulebuilder.hpp>

#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace asllvm::detail
{

class ModuleMap
{
public:
	ModuleBuilder& operator[](std::string_view name);

	void dump_state() const;

private:
	std::unordered_map<std::string, ModuleBuilder> m_map;
};

}
