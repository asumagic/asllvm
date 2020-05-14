#include <asllvm/detail/modulemap.hpp>

#include <asllvm/detail/jitcompiler.hpp>
#include <asllvm/detail/llvmglobals.hpp>
#include <asllvm/jit.hpp>
#include <fmt/core.h>

namespace asllvm::detail
{
ModuleMap::ModuleMap(JitCompiler& compiler) : m_compiler{compiler}, m_shared_module_builder(m_compiler) {}

ModuleBuilder& ModuleMap::operator[](asIScriptModule* module)
{
	if (module == nullptr)
	{
		return m_shared_module_builder;
	}

	const char* name = module->GetName();

	if (auto it = m_map.find(name); it != m_map.end())
	{
		return it->second;
	}

	const auto [it, success] = m_map.emplace(std::string(name), ModuleBuilder{m_compiler, module});

	return it->second;
}

void ModuleMap::build_modules()
{
	if (m_compiler.config().verbose)
	{
		m_compiler.diagnostic("building modules");
	}

	m_shared_module_builder.build();
	for (auto& it : m_map)
	{
		it.second.build();
	}

	if (m_compiler.config().verbose)
	{
		m_compiler.diagnostic("linking modules");
	}

	m_shared_module_builder.link();
	for (auto& it : m_map)
	{
		it.second.link();
	}

	m_map.clear();
}

void ModuleMap::dump_state() const
{
	for (const auto& [name, module_builder] : m_map)
	{
		fmt::print(stderr, "\nModule '{}':\n", name);
		module_builder.dump_state();
	}
}

} // namespace asllvm::detail
