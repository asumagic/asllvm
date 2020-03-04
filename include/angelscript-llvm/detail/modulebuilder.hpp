#pragma once

#include <angelscript.h>

// TODO: we can get rid of this dependency, we just need some trickery due to std::unique_ptr requiring the definition
//       of llvm::Module to be present because of the deleter. this can be worked around.
//       this is included in jit.hpp, we do not want LLVM stuff included there.
#include <llvm/IR/Module.h>

#include <memory>
#include <string_view>

namespace asllvm::detail
{

class ModuleBuilder
{
public:
	ModuleBuilder(std::string_view angelscript_module_name);

	llvm::Function* create_function(asIScriptFunction& function);

	void dump_state() const;

private:
	std::unique_ptr<llvm::Module> m_module;
};

}
