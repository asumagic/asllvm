#pragma once

#include <asllvm/detail/fwd.hpp>
#include <asllvm/detail/modulebuilder.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace asllvm::detail
{
class ModuleMap
{
	public:
	ModuleMap(JitCompiler& compiler);

	ModuleBuilder& operator[](asIScriptModule* module);

	void build_modules();

	void dump_state() const;

	private:
	JitCompiler& m_compiler;

	ModuleBuilder                                  m_shared_module_builder;
	std::unordered_map<std::string, ModuleBuilder> m_map;
};

} // namespace asllvm::detail
