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

llvm::Function* ModuleBuilder::get_script_function(asCScriptFunction& function)
{
	asllvm_assert(
		function.vfTableIdx < 0
		&& "Virtual functions should not be handled at this level. Resolve the virtual function first.");

	if (auto it = m_script_functions.find(function.GetId()); it != m_script_functions.end())
	{
		return it->second;
	}

	if (m_compiler.config().verbose)
	{
		m_compiler.diagnostic(fmt::format(
			"creating function {}: signature {}, object type {}",
			function.GetId(),
			function.GetDeclaration(true, true, true),
			function.objectType != nullptr ? function.objectType->GetName() : "(null)"));
	}

	const std::string name = make_function_name(function);

	const auto create_function = [&](const auto& name, auto linkage) {
		llvm::Function* llvm_function
			= llvm::Function::Create(get_script_function_type(function), linkage, name, *m_llvm_module);

		// i8* noalias %params
		(llvm_function->arg_begin() + 0)->setName("params");
		llvm_function->addParamAttr(0, llvm::Attribute::NoAlias);
		llvm_function->addParamAttr(0, llvm::Attribute::NoCapture);

		return llvm_function;
	};

	// It apparently helps LLVM optimizations to split the same function into:
	// 1. one that has an internal linkage
	// 2. one that has an external linkage that calls the former
	// The reason is that the function with the internal linkage can have its function type transformed freely, enabling
	// sometimes powerful optimizations. It does not seem to be able to infer that when the function is external since
	// the function type must stay the same, but it is also not able to do the "split" on its own.

	llvm::Function* internal_function
		= create_function(fmt::format("{}.internal", name), llvm::Function::InternalLinkage);

	llvm::Function* proxy_function = create_function(name, llvm::Function::ExternalLinkage);

	{
		// TODO: move to its own function, or find a better way to do this
		llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
		llvm::LLVMContext& context = m_compiler.builder().context();

		auto* old_bb           = ir.GetInsertBlock();
		auto  old_insert_point = ir.GetInsertPoint();

		ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", proxy_function));

		std::array<llvm::Value*, 1> args{&*(proxy_function->arg_begin() + 0)};
		llvm::Value*                ret = ir.CreateCall(internal_function->getFunctionType(), internal_function, args);

		if (proxy_function->getReturnType() != llvm::Type::getVoidTy(context))
		{
			ir.CreateRet(ret);
		}
		else
		{
			ir.CreateRetVoid();
		}

		if (old_bb != nullptr)
		{
			ir.SetInsertPoint(old_bb, old_insert_point);
		}
	}

	m_script_functions.emplace(function.GetId(), internal_function);

	return internal_function;
}

llvm::FunctionType* ModuleBuilder::get_script_function_type(asCScriptFunction& script_function)
{
	CommonDefinitions& defs = m_compiler.builder().definitions();

	std::array<llvm::Type*, 1> types{llvm::PointerType::getInt32PtrTy(m_compiler.builder().context())};

	// If returning on stack, this means the return object will be written to a pointer passed as a param (using PSF).
	// Otherwise, an handle to the actual object is returned.
	// That is, of course, unless the return type is void, in which case we don't need to care.
	llvm::Type* return_type;

	if (script_function.returnType.GetTokenType() == ttVoid || script_function.DoesReturnOnStack())
	{
		return_type = defs.tvoid;
	}
	else if (script_function.returnType.IsObject() && !script_function.returnType.IsObjectHandle())
	{
		return_type = m_compiler.builder().to_llvm_type(script_function.returnType)->getPointerTo();
	}
	else
	{
		return_type = m_compiler.builder().to_llvm_type(script_function.returnType);
	}

	return llvm::FunctionType::get(return_type, types, false);
}

llvm::Function* ModuleBuilder::get_system_function(asCScriptFunction& system_function)
{
	asSSystemFunctionInterface& intf = *system_function.sysFuncIntf;

	const int id = system_function.GetId();
	if (auto it = m_system_functions.find(id); it != m_system_functions.end())
	{
		return it->second;
	}

	llvm::Function* function = llvm::Function::Create(
		get_system_function_type(system_function),
		llvm::Function::ExternalLinkage,
		0,
		make_system_function_name(system_function),
		m_llvm_module.get());

	if (intf.hostReturnInMemory)
	{
		function->addParamAttr(0, llvm::Attribute::StructRet);
	}

	if (m_compiler.config().assume_const_is_pure && system_function.IsReadOnly())
	{
		function->addFnAttr(llvm::Attribute::InaccessibleMemOrArgMemOnly);
	}

	m_system_functions.emplace(id, function);

	return function;
}

llvm::FunctionType* ModuleBuilder::get_system_function_type(asCScriptFunction& system_function)
{
	CommonDefinitions&          defs = m_compiler.builder().definitions();
	asSSystemFunctionInterface& intf = *system_function.sysFuncIntf;

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
	case ICC_VIRTUAL_THISCALL:
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

	return llvm::FunctionType::get(return_type, types, false);
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

	for (JitSymbol& symbol : m_jit_functions)
	{
		auto function = ExitOnError(m_compiler.jit().lookup(symbol.name));
		symbol.script_function->SetUserData(reinterpret_cast<void*>(function.getAddress()), vtable_userdata_identifier);

		auto entry           = ExitOnError(m_compiler.jit().lookup(symbol.entry_name));
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
	return reinterpret_cast<void*>(
		object_type.virtualFunctionTable[function->vfTableIdx]->GetUserData(vtable_userdata_identifier));
}

void* ModuleBuilder::system_vtable_lookup(void* object, asPWORD func)
{
	// TODO: this likely does not have to be a function
#if defined(__linux__) && defined(__x86_64__)
	using FunctionPtr = asQWORD (*)();
	auto vftable      = *(reinterpret_cast<FunctionPtr**>(object));
	return reinterpret_cast<void*>(vftable[func >> 3]);
#else
#	error("virtual function lookups unsupported for this target")
#endif
}

void ModuleBuilder::call_object_method(void* object, asCScriptFunction* function)
{
	// TODO: this is not very efficient: this performs an extra call into AS that is more generic than we require: we
	// know a lot of stuff about the function at JIT time.
	auto& engine = *static_cast<asCScriptEngine*>(function->GetEngine());
	engine.CallObjectMethod(object, function->GetId());
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

		funcs.alloc->addFnAttr(llvm::Attribute::InaccessibleMemOnly);

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

		funcs.script_object_constructor->addFnAttr(llvm::Attribute::InaccessibleMemOrArgMemOnly);
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

	{
		std::array<llvm::Type*, 2> types{{defs.pvoid, defs.pvoid}};

		llvm::FunctionType* function_type = llvm::FunctionType::get(defs.tvoid, types, false);
		funcs.call_object_method          = llvm::Function::Create(
			function_type,
			llvm::Function::ExternalLinkage,
			0,
			"asllvm.private.call_object_method",
			m_llvm_module.get());
	}

	return funcs;
}

void ModuleBuilder::build_functions()
{
	for (auto& pending : m_pending_functions)
	{
		get_script_function(*static_cast<asCScriptFunction*>(pending.function));
	}

	for (const auto& pending : m_pending_functions)
	{
		FunctionBuilder builder{
			m_compiler,
			*this,
			*static_cast<asCScriptFunction*>(pending.function),
			m_script_functions.at(pending.function->GetId())};

		asUINT   length;
		asDWORD* bytecode = pending.function->GetByteCode(&length);

		builder.read_bytecode(bytecode, length);

		llvm::Function* entry = builder.create_vm_entry();

		JitSymbol symbol;
		symbol.script_function = pending.function;
		symbol.name            = make_function_name(*pending.function); // TODO: get it from somewhere
		symbol.entry_name      = entry->getName();
		symbol.jit_function    = pending.jit_function;
		m_jit_functions.push_back(symbol);
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
	define_function(call_object_method, "asllvm.private.call_object_method");

	define_function(fmodf, "fmodf");
	define_function(fmod, "fmod");
}
} // namespace asllvm::detail
