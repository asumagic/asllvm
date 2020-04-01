#pragma once

#include <angelscript-llvm/detail/builder.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace asllvm
{
class JitCompiler;
}

namespace asllvm::detail
{
class ModuleMap
{
	public:
	ModuleMap(JitCompiler& compiler);

	ModuleBuilder& operator[](std::string_view name);

	void dump_state() const;

	private:
	JitCompiler& m_compiler;

	std::unordered_map<std::string, ModuleBuilder> m_map;
};

} // namespace asllvm::detail
