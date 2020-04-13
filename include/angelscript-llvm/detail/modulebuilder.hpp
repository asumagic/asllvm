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
	llvm::Function *alloc, *script_object_constructor, *vtable_lookup;
};

struct PendingFunction
{
	asIScriptFunction* function;
	asJITFunction*     jit_function;
};

struct JitSymbol
{
	std::string    name;
	asJITFunction* jit_function;
};

class ModuleBuilder
{
	public:
	ModuleBuilder(JitCompiler& compiler, asIScriptModule& module);

	void append(PendingFunction function);

	llvm::Function* create_function(asCScriptFunction& function);
	llvm::Function* get_system_function(asCScriptFunction& system_function);

	void build();

	llvm::Module&      module() { return *m_llvm_module; }
	InternalFunctions& internal_functions() { return m_internal_functions; }

	void dump_state() const;

	private:
	// TODO: move this elsewhere, potentially
	static void* virtual_table_lookup(asCScriptObject* object, asCScriptFunction* function);

	bool is_exposed_directly(asIScriptFunction& function) const;

	InternalFunctions setup_internal_functions();

	void build_functions();
	void link_symbols();

	JitCompiler&                   m_compiler;
	asIScriptModule*               m_script_module;
	std::unique_ptr<llvm::Module>  m_llvm_module;
	std::vector<PendingFunction>   m_pending_functions;
	std::vector<JitSymbol>         m_jit_functions;
	std::map<int, llvm::Function*> m_script_functions;
	std::map<int, llvm::Function*> m_system_functions;
	InternalFunctions              m_internal_functions;
};

} // namespace asllvm::detail
