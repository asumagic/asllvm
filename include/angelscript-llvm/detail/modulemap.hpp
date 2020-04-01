#pragma once

#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/builder.hpp>

#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace asllvm::detail
{

class ModuleMap
{
public:
	ModuleMap(Builder& builder);

	ModuleBuilder& operator[](std::string_view name);

	void dump_state() const;

private:
	Builder* m_builder;

	std::unordered_map<std::string, ModuleBuilder> m_map;
};

}
