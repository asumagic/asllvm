#pragma once

// TODO: we can get rid of this dependency, we just need some trickery due to std::unique_ptr requiring the definition
//       of llvm::Module to be present because of the deleter. this can be worked around.
//       this is included in jit.hpp, we do not want LLVM stuff included there.
#include <llvm/IR/Module.h>

#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/fwd.hpp>
#include <angelscript.h>
#include <llvm/IR/PassManager.h>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace asllvm::detail
{
struct InternalFunctions
{
	llvm::Function *alloc, *script_object_constructor;
};

class ModuleBuilder
{
	public:
	ModuleBuilder(JitCompiler& compiler, std::string_view angelscript_module_name);

	void            add_jit_function(std::string name, asJITFunction* function);
	FunctionBuilder create_function_builder(asCScriptFunction& function);
	llvm::Function* create_function(asCScriptFunction& function);

	llvm::Function* get_system_function(asCScriptFunction& system_function);

	void build();

	llvm::Module&      module() { return *m_module; }
	InternalFunctions& internal_functions() { return m_internal_functions; }

	void dump_state() const;

	private:
	InternalFunctions setup_internal_functions();

	JitCompiler&                                        m_compiler;
	std::unique_ptr<llvm::Module>                       m_module;
	std::vector<std::pair<std::string, asJITFunction*>> m_jit_functions;
	std::map<int, llvm::Function*>                      m_script_functions;
	std::map<int, llvm::Function*>                      m_system_functions;
	InternalFunctions                                   m_internal_functions;
};

} // namespace asllvm::detail
