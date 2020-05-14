#pragma once

// TODO: we can get rid of this dependency, we just need some trickery due to std::unique_ptr requiring the definition
//       of llvm::Module to be present because of the deleter. this can be worked around.
//       this is included in jit.hpp, we do not want LLVM stuff included there.
#include <llvm/IR/Module.h>

#include <asllvm/detail/asinternalheaders.hpp>
#include <asllvm/detail/fwd.hpp>
#include <angelscript.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/PassManager.h>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace asllvm::detail
{
struct Runtime
{
	llvm::FunctionCallee alloc, free, new_script_object, script_vtable_lookup, system_vtable_lookup, call_object_method;
};

struct PendingFunction
{
	asCScriptFunction* function;
	asJITFunction*     jit_function;
};

struct JitSymbol
{
	asCScriptFunction* script_function;
	std::string        name, entry_name;
	asJITFunction*     jit_function;
};

struct ModuleDebugInfo
{
	llvm::DICompileUnit* compile_unit;
	llvm::DIFile*        file;

	using AsTypeIdentifier = int;
	std::unordered_map<AsTypeIdentifier, llvm::DIType*> type_cache;
};

class ModuleBuilder
{
	public:
	ModuleBuilder(JitCompiler& compiler, asIScriptModule* module = nullptr);

	void append(PendingFunction function);

	llvm::Function*     get_script_function(const asCScriptFunction& function);
	llvm::FunctionType* get_script_function_type(const asCScriptFunction& script_function);

	llvm::Function*     get_system_function(const asCScriptFunction& system_function);
	llvm::FunctionType* get_system_function_type(const asCScriptFunction& system_function);

	llvm::DIType* get_debug_type(ModuleDebugInfo::AsTypeIdentifier type);

	void build();
	void link();

	llvm::Module& module() { return *m_llvm_module; }
	Runtime&      runtime() { return m_internal_functions; }

	llvm::DIBuilder& di_builder() { return *m_di_builder; }
	ModuleDebugInfo& debug_info() { return m_debug_info; }

	void dump_state() const;

	private:
	bool is_exposed_directly(asIScriptFunction& function) const;

	ModuleDebugInfo setup_debug_info();
	Runtime         setup_runtime();

	void build_functions();
	void link_symbols();

	JitCompiler&                     m_compiler;
	asIScriptModule*                 m_script_module;
	std::unique_ptr<llvm::Module>    m_llvm_module;
	std::unique_ptr<llvm::DIBuilder> m_di_builder;
	ModuleDebugInfo                  m_debug_info;
	std::vector<PendingFunction>     m_pending_functions;
	std::vector<JitSymbol>           m_jit_functions;
	std::map<int, llvm::Function*>   m_script_functions;
	std::map<int, llvm::Function*>   m_system_functions;
	Runtime                          m_internal_functions;
};

} // namespace asllvm::detail
