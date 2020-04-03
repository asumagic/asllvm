#pragma once

#include <angelscript.h>

#include <angelscript-llvm/detail/fwd.hpp>

// TODO: we can get rid of this dependency, we just need some trickery due to std::unique_ptr requiring the definition
//       of llvm::Module to be present because of the deleter. this can be worked around.
//       this is included in jit.hpp, we do not want LLVM stuff included there.
#include <llvm/IR/Module.h>

#include <llvm/IR/PassManager.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace asllvm::detail
{
class ModuleBuilder
{
	public:
	ModuleBuilder(JitCompiler& compiler, std::string_view angelscript_module_name);

	void            add_jit_function(std::string name, asJITFunction* function);
	FunctionBuilder create_function_builder(asIScriptFunction& function, asJITFunction& jit_function_output);
	llvm::Function* create_function(asIScriptFunction& function);

	llvm::Function* get_system_function(asIScriptFunction& system_function);

	void build();

	llvm::Module& module() { return *m_module; }

	void dump_state() const;

	private:
	JitCompiler&                                        m_compiler;
	std::unique_ptr<llvm::Module>                       m_module;
	std::vector<std::pair<std::string, asJITFunction*>> m_jit_functions;
	std::map<int, llvm::Function*>                      m_script_functions;
	std::map<int, llvm::Function*>                      m_system_functions;
};

} // namespace asllvm::detail
