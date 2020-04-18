#include <angelscript-llvm/detail/modulebuilder.hpp>

#include <angelscript-llvm/detail/assert.hpp>
#include <angelscript-llvm/detail/functionbuilder.hpp>
#include <angelscript-llvm/detail/jitcompiler.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <array>
#include <fmt/core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>

namespace asllvm::detail
{
ModuleBuilder::ModuleBuilder(JitCompiler& compiler, asIScriptModule& module) :
	m_compiler{compiler},
	m_script_module{&module},
	m_llvm_module{std::make_unique<llvm::Module>(make_module_name(module), compiler.builder().context())},
	m_internal_functions{setup_internal_functions()}
{}

void ModuleBuilder::append(PendingFunction function) { m_pending_functions.push_back(function); }

llvm::Function* ModuleBuilder::create_function(asCScriptFunction& function)
{
	if (auto it = m_script_functions.find(function.GetId()); it != m_script_functions.end())
	{
		return it->second;
	}

	std::array<llvm::Type*, 1> types{llvm::PointerType::getInt32PtrTy(m_compiler.builder().context())};

	// If returning on stack, this means the return object will be written to a pointer passed as a param (using PSF).
	llvm::Type* return_type = function.DoesReturnOnStack() ? m_compiler.builder().definitions().tvoid
														   : m_compiler.builder().to_llvm_type(function.returnType);

	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, types, false);

	const std::string name = make_function_name(function);

	llvm::Function* llvm_function = llvm::Function::Create(
		function_type,
		is_exposed_directly(function) ? llvm::Function::ExternalLinkage : llvm::Function::InternalLinkage,
		name,
		*m_llvm_module);

	// i8* noalias %params
	(llvm_function->arg_begin() + 0)->setName("params");
	llvm_function->addParamAttr(0, llvm::Attribute::NoAlias);

	m_script_functions.emplace(function.GetId(), llvm_function);

	return llvm_function;
}

llvm::Function* ModuleBuilder::get_system_function(asCScriptFunction& system_function)
{
	CommonDefinitions&          defs = m_compiler.builder().definitions();
	asSSystemFunctionInterface& intf = *system_function.sysFuncIntf;

	const int id = system_function.GetId();

	if (auto it = m_system_functions.find(id); it != m_system_functions.end())
	{
		return it->second;
	}

	llvm::Type* return_type = defs.tvoid;

	const std::size_t        param_count = system_function.GetParamCount();
	std::vector<llvm::Type*> types;

	if (intf.hostReturnInMemory)
	{
		// types[0]
		types.push_back(m_compiler.builder().to_llvm_type(system_function.returnType)->getPointerTo());
	}
	else
	{
		return_type = m_compiler.builder().to_llvm_type(system_function.returnType);
	}

	for (std::size_t i = 0; i < param_count; ++i)
	{
		types.push_back(m_compiler.builder().to_llvm_type(system_function.parameterTypes[i]));
	}

	switch (intf.callConv)
	{
	// thiscall: add this as first parameter
	// HACK: this is probably not very crossplatform to do
	case ICC_THISCALL:
	case ICC_CDECL_OBJFIRST:
	{
		types.insert(types.begin(), defs.pvoid);
		break;
	}

	case ICC_CDECL_OBJLAST:
	{
		types.push_back(defs.pvoid);
		break;
	}

	// C calling convention: nothing special to do
	case ICC_CDECL: break;

	default: asllvm_assert(false && "unsupported calling convention");
	}

	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, types, false);

	llvm::Function* function = llvm::Function::Create(
		function_type,
		llvm::Function::ExternalLinkage,
		0,
		make_system_function_name(system_function),
		m_llvm_module.get());

	if (intf.hostReturnInMemory)
	{
		function->addParamAttr(0, llvm::Attribute::StructRet);
	}

	m_system_functions.emplace(id, function);

	return function;
}

void ModuleBuilder::build()
{
	build_functions();

	if (m_compiler.config().verbose)
	{
		dump_state();
	}

	link_symbols();

	m_compiler.builder().optimizer().run(*m_llvm_module);

	ExitOnError(m_compiler.jit().addIRModule(
		llvm::orc::ThreadSafeModule(std::move(m_llvm_module), m_compiler.builder().extract_old_context())));

	for (auto& symbol : m_jit_functions)
	{
		auto entry           = ExitOnError(m_compiler.jit().lookup(symbol.name));
		*symbol.jit_function = reinterpret_cast<asJITFunction>(entry.getAddress());
	}
}

void ModuleBuilder::dump_state() const
{
	for (const auto& function : m_llvm_module->functions())
	{
		fmt::print(stderr, "Function '{}'\n", function.getName().str());
	}

	m_llvm_module->print(llvm::errs(), nullptr);
}

void* ModuleBuilder::script_vtable_lookup(asCScriptObject* object, asCScriptFunction* function)
{
	auto& object_type = *static_cast<asCObjectType*>(object->GetObjectType());
	return reinterpret_cast<void*>(object_type.virtualFunctionTable[function->vfTableIdx]->scriptData->jitFunction);
}

void* ModuleBuilder::system_vtable_lookup(void* object, asFUNCTION_t func)
{
#if defined(__linux__) && defined(__x86_64__)
	using FunctionPtr = asQWORD (*)();
	auto vftable      = *(reinterpret_cast<FunctionPtr**>(object));
	return reinterpret_cast<void*>(vftable[FuncPtrToUInt(func) >> 3]);
#else
#	error("virtual function lookups unsupported for this target")
#endif
}

bool ModuleBuilder::is_exposed_directly(asIScriptFunction& function) const
{
	return function.GetObjectType() != nullptr;
}

InternalFunctions ModuleBuilder::setup_internal_functions()
{
	CommonDefinitions& defs = m_compiler.builder().definitions();

	InternalFunctions funcs{};

	{
		std::array<llvm::Type*, 1> types{{defs.iptr}};
		llvm::Type*                return_type = defs.pvoid;

		llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, types, false);
		funcs.alloc                       = llvm::Function::Create(
			function_type, llvm::Function::ExternalLinkage, 0, "asllvm.private.alloc", m_llvm_module.get());

		// The object we created is unique as it was dynamically allocated
		funcs.alloc->addAttribute(0, llvm::Attribute::NoAlias);
	}

	{
		std::array<llvm::Type*, 1> types{{defs.pvoid}};
		llvm::Type*                return_type = defs.tvoid;

		llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, types, false);
		funcs.free                        = llvm::Function::Create(
			function_type, llvm::Function::ExternalLinkage, 0, "asllvm.private.free", m_llvm_module.get());
	}

	{
		std::array<llvm::Type*, 2> types{{defs.pvoid, defs.pvoid}};

		llvm::FunctionType* function_type = llvm::FunctionType::get(defs.tvoid, types, false);
		funcs.script_object_constructor   = llvm::Function::Create(
			function_type,
			llvm::Function::ExternalLinkage,
			0,
			"asllvm.private.script_object_constructor",
			m_llvm_module.get());
	}

	{
		std::array<llvm::Type*, 2> types{{defs.pvoid, defs.pvoid}};

		llvm::FunctionType* function_type = llvm::FunctionType::get(defs.pvoid, types, false);
		funcs.script_vtable_lookup        = llvm::Function::Create(
			function_type,
			llvm::Function::ExternalLinkage,
			0,
			"asllvm.private.script_vtable_lookup",
			m_llvm_module.get());
	}

	{
		std::array<llvm::Type*, 2> types{{defs.pvoid, defs.pvoid}};

		llvm::FunctionType* function_type = llvm::FunctionType::get(defs.pvoid, types, false);
		funcs.system_vtable_lookup        = llvm::Function::Create(
			function_type,
			llvm::Function::ExternalLinkage,
			0,
			"asllvm.private.system_vtable_lookup",
			m_llvm_module.get());
	}

	return funcs;
}

void ModuleBuilder::build_functions()
{
	for (auto& pending : m_pending_functions)
	{
		create_function(*static_cast<asCScriptFunction*>(pending.function));
	}

	for (const auto& pending : m_pending_functions)
	{
		FunctionBuilder builder{m_compiler,
								*this,
								*static_cast<asCScriptFunction*>(pending.function),
								m_script_functions.at(pending.function->GetId())};

		asUINT   length;
		asDWORD* bytecode = pending.function->GetByteCode(&length);

		builder.read_bytecode(bytecode, length);

		llvm::Function* entry = builder.create_wrapper_function();
		m_jit_functions.push_back({entry->getName().str(), pending.jit_function});
	}

	m_pending_functions.clear();
}

void ModuleBuilder::link_symbols()
{
	const auto define_function = [&](auto* address, const std::string& name) {
		llvm::JITEvaluatedSymbol symbol(llvm::pointerToJITTargetAddress(address), llvm::JITSymbolFlags::Callable);

		ExitOnError(m_compiler.jit().defineAbsolute(name, symbol));
	};

	for (const auto& it : m_system_functions)
	{
		auto& script_func = static_cast<asCScriptFunction&>(*m_compiler.engine().GetFunctionById(it.first));
		define_function(script_func.sysFuncIntf->func, it.second->getName());
	}

	// TODO: figure out why func->getName() returns an empty string
	define_function(*userAlloc, "asllvm.private.alloc");
	define_function(*userFree, "asllvm.private.free");
	define_function(ScriptObject_Construct, "asllvm.private.script_object_constructor");
	define_function(script_vtable_lookup, "asllvm.private.script_vtable_lookup");
	define_function(system_vtable_lookup, "asllvm.private.system_vtable_lookup");

	define_function(fmodf, "fmodf");
	define_function(fmod, "fmod");
}
} // namespace asllvm::detail
