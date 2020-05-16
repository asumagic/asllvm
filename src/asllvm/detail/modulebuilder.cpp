#include <asllvm/detail/modulebuilder.hpp>

#include <asllvm/detail/assert.hpp>
#include <asllvm/detail/functionbuilder.hpp>
#include <asllvm/detail/jitcompiler.hpp>
#include <asllvm/detail/llvmglobals.hpp>
#include <asllvm/detail/modulecommon.hpp>
#include <asllvm/detail/runtime.hpp>
#include <asllvm/detail/vmstate.hpp>
#include <fmt/core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>

namespace asllvm::detail
{
ModuleBuilder::ModuleBuilder(JitCompiler& compiler, asIScriptModule* module) :
	m_compiler{compiler},
	m_script_module{module},
	m_llvm_module{
		std::make_unique<llvm::Module>(make_module_name(module), *compiler.builder().llvm_context().getContext())},
	m_di_builder{std::make_unique<llvm::DIBuilder>(*m_llvm_module)},
	m_debug_info{setup_debug_info()},
	m_internal_functions{setup_runtime()},
	m_global_variables{setup_global_variables()}
{}

void ModuleBuilder::append(PendingFunction function) { m_pending_functions.push_back(function); }

llvm::Function* ModuleBuilder::get_script_function(const asCScriptFunction& function)
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

	llvm::Function* internal_function = llvm::Function::Create(
		get_script_function_type(function),
		llvm::Function::ExternalLinkage,
		make_function_name(function),
		*m_llvm_module);

	m_script_functions.emplace(function.GetId(), internal_function);
	return internal_function;
}

llvm::FunctionType* ModuleBuilder::get_script_function_type(const asCScriptFunction& script_function)
{
	asCScriptEngine& engine  = m_compiler.engine();
	Builder&         builder = m_compiler.builder();
	StandardTypes&   types   = builder.standard_types();

	const auto parameter_count = script_function.parameterTypes.GetLength();

	std::vector<llvm::Type*> parameter_types;

	// TODO: make sret
	if (script_function.returnType.GetTokenType() != ttVoid)
	{
		if (script_function.DoesReturnOnStack())
		{
			parameter_types.push_back(builder.to_llvm_type(script_function.returnType));
		}
		else
		{
			parameter_types.push_back(builder.to_llvm_type(script_function.returnType)->getPointerTo());
		}
	}

	if (asCObjectType* object_type = script_function.objectType; object_type != nullptr)
	{
		parameter_types.push_back(
			builder.to_llvm_type(engine.GetDataTypeFromTypeId(script_function.objectType->GetTypeId())));
	}

	for (std::size_t i = 0; i < parameter_count; ++i)
	{
		parameter_types.push_back(builder.to_llvm_type(script_function.parameterTypes[i]));
	}

	return llvm::FunctionType::get(types.vm_state, parameter_types, false);
}

llvm::Function* ModuleBuilder::get_system_function(const asCScriptFunction& system_function)
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

llvm::FunctionType* ModuleBuilder::get_system_function_type(const asCScriptFunction& system_function)
{
	StandardTypes&              types = m_compiler.builder().standard_types();
	asSSystemFunctionInterface& intf  = *system_function.sysFuncIntf;

	llvm::Type* return_type = types.tvoid;

	const std::size_t        param_count = system_function.GetParamCount();
	std::vector<llvm::Type*> parameter_types;

	if (intf.hostReturnInMemory)
	{
		// types[0]
		parameter_types.push_back(m_compiler.builder().to_llvm_type(system_function.returnType)->getPointerTo());
	}
	else
	{
		return_type = m_compiler.builder().to_llvm_type(system_function.returnType);
	}

	for (std::size_t i = 0; i < param_count; ++i)
	{
		parameter_types.push_back(m_compiler.builder().to_llvm_type(system_function.parameterTypes[i]));
	}

	switch (intf.callConv)
	{
	// thiscall: add this as first parameter
	// HACK: this is probably not very crossplatform to do
	case ICC_VIRTUAL_THISCALL:
	case ICC_THISCALL:
	case ICC_CDECL_OBJFIRST:
	{
		parameter_types.insert(parameter_types.begin(), types.pvoid);
		break;
	}

	case ICC_CDECL_OBJLAST:
	{
		parameter_types.push_back(types.pvoid);
		break;
	}

	// C calling convention: nothing special to do
	case ICC_CDECL: break;

	default: asllvm_assert(false && "unsupported calling convention");
	}

	return llvm::FunctionType::get(return_type, parameter_types, false);
}

llvm::DIType* ModuleBuilder::get_debug_type(ModuleDebugInfo::AsTypeIdentifier script_type_id)
{
	asCScriptEngine& engine = m_compiler.engine();

	if (const auto it = m_debug_info.type_cache.find(script_type_id); it != m_debug_info.type_cache.end())
	{
		return it->second;
	}

	const auto add = [&](llvm::DIType* debug_type) {
		m_debug_info.type_cache.emplace(script_type_id, debug_type);
		return debug_type;
	};

	switch (script_type_id)
	{
	case asTYPEID_VOID: return add(m_di_builder->createUnspecifiedType("void"));

	case asTYPEID_BOOL: return add(m_di_builder->createBasicType("bool", 1, llvm::dwarf::DW_ATE_boolean));

	case asTYPEID_INT8: return add(m_di_builder->createBasicType("int8", 8, llvm::dwarf::DW_ATE_signed));
	case asTYPEID_INT16: return add(m_di_builder->createBasicType("int16", 16, llvm::dwarf::DW_ATE_signed));
	case asTYPEID_INT32: return add(m_di_builder->createBasicType("int", 32, llvm::dwarf::DW_ATE_signed));
	case asTYPEID_INT64: return add(m_di_builder->createBasicType("int64", 64, llvm::dwarf::DW_ATE_signed));

	case asTYPEID_UINT8: return add(m_di_builder->createBasicType("uint8", 8, llvm::dwarf::DW_ATE_unsigned));
	case asTYPEID_UINT16: return add(m_di_builder->createBasicType("uint16", 16, llvm::dwarf::DW_ATE_unsigned));
	case asTYPEID_UINT32: return add(m_di_builder->createBasicType("uint", 32, llvm::dwarf::DW_ATE_unsigned));
	case asTYPEID_UINT64: return add(m_di_builder->createBasicType("uint64", 64, llvm::dwarf::DW_ATE_unsigned));

	case asTYPEID_FLOAT: return add(m_di_builder->createBasicType("float", 32, llvm::dwarf::DW_ATE_float));
	case asTYPEID_DOUBLE: return add(m_di_builder->createBasicType("double", 64, llvm::dwarf::DW_ATE_float));
	}

	asITypeInfo* type_info = engine.GetTypeInfoById(script_type_id);
	asllvm_assert(type_info != nullptr);

	if (const auto flags = type_info->GetFlags(); (flags & asOBJ_SCRIPT_OBJECT) != 0)
	{
		const auto& object_type = *static_cast<asCObjectType*>(type_info);

		std::vector<llvm::Metadata*> llvm_properties;

		const auto& properties = object_type.properties;
		for (std::size_t i = 0; i < properties.GetLength(); ++i)
		{
			const auto& property = properties[i];

			llvm_properties.push_back(m_di_builder->createMemberType(
				nullptr,
				&property->name[0],
				nullptr,
				0,
				property->type.GetSizeInMemoryBytes() * 8,
				0,
				property->byteOffset * 8,
				llvm::DINode::FlagPublic,
				get_debug_type(engine.GetTypeIdFromDataType(property->type))));
		}

		llvm::DIType* class_type = m_di_builder->createClassType(
			nullptr,
			object_type.GetName(),
			nullptr,
			0,
			object_type.GetSize(),
			0,
			0,
			llvm::DINode::FlagTypePassByReference,
			nullptr,
			m_di_builder->getOrCreateArray(llvm_properties));

		return m_di_builder->createPointerType(class_type, AS_PTR_SIZE * 4 * 8);
	}
	else if ((flags & (asOBJ_REF | asOBJ_VALUE)) != 0)
	{
		return add(
			m_di_builder->createBasicType(type_info->GetName(), AS_PTR_SIZE * 4 * 8, llvm::dwarf::DW_ATE_address));
	}

	return add(m_di_builder->createUnspecifiedType("<unimplemented>"));
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
		llvm::orc::ThreadSafeModule(std::move(m_llvm_module), m_compiler.builder().llvm_context())));
}

void ModuleBuilder::link()
{
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

ModuleDebugInfo ModuleBuilder::setup_debug_info()
{
	ModuleDebugInfo debug_info;

	debug_info.file
		= m_di_builder->createFile(m_script_module != nullptr ? m_script_module->GetName() : "<shared>", "");

	debug_info.compile_unit = m_di_builder->createCompileUnit(
		llvm::dwarf::DW_LANG_C_plus_plus,
		debug_info.file,
		"asllvm",
		m_compiler.config().allow_llvm_optimizations,
		"",
		0);

	return debug_info;
}

StandardFunctions ModuleBuilder::setup_runtime()
{
	StandardTypes& types = m_compiler.builder().standard_types();

	StandardFunctions funcs{};

	const auto linkage = llvm::Function::ExternalLinkage;

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.pvoid, {types.iptr}, false),
			linkage,
			"asllvm.private.alloc",
			m_llvm_module.get());

		// The object we created is unique as it was dynamically allocated
		function->addAttribute(0, llvm::Attribute::NoAlias);

		function->setOnlyAccessesInaccessibleMemory();

		funcs.alloc = function;
	}

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.tvoid, {types.pvoid}, false),
			linkage,
			"asllvm.private.free",
			m_llvm_module.get());

		funcs.free = function;
	}

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.pvoid, {types.pvoid}, false),
			linkage,
			"asllvm.private.new_script_object",
			m_llvm_module.get());

		function->setOnlyAccessesInaccessibleMemOrArgMem();

		// The object we created is unique as it was dynamically allocated
		function->addAttribute(0, llvm::Attribute::NoAlias);

		funcs.new_script_object = function;
	}

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.pvoid, {types.pvoid, types.pvoid}, false),
			linkage,
			"asllvm.private.script_vtable_lookup",
			m_llvm_module.get());

		funcs.script_vtable_lookup = function;
	}

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.pvoid, {types.pvoid, types.pvoid}, false),
			linkage,
			0,
			"asllvm.private.system_vtable_lookup",
			m_llvm_module.get());

		funcs.system_vtable_lookup = function;
	}

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.tvoid, {types.pvoid, types.pvoid}, false),
			linkage,
			"asllvm.private.call_object_method",
			m_llvm_module.get());

		funcs.call_object_method = function;
	}

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.tvoid, {}, false), linkage, "asllvm.private.panic", m_llvm_module.get());

		function->setDoesNotReturn();

		funcs.panic = function;
	}

	{
		llvm::Function* function = llvm::Function::Create(
			llvm::FunctionType::get(types.tvoid, {types.vm_state}, false),
			linkage,
			"asllvm.private.set_internal_exception",
			m_llvm_module.get());

		funcs.set_internal_exception = function;
	}

	return funcs;
}

GlobalVariables ModuleBuilder::setup_global_variables()
{
	GlobalVariables globals;

	return globals;
}

void ModuleBuilder::build_functions()
{
	for (const auto& pending : m_pending_functions)
	{
		if (std::find_if(
				m_jit_functions.begin(),
				m_jit_functions.end(),
				[&](JitSymbol symbol) { return symbol.script_function == pending.function; })
			!= m_jit_functions.end())
		{
			if (m_compiler.config().verbose)
			{
				m_compiler.diagnostic("ignoring function that was compiled in module already");
			}

			continue;
		}

		FunctionContext context;
		context.compiler        = &m_compiler;
		context.module_builder  = this;
		context.script_function = static_cast<asCScriptFunction*>(pending.function);
		context.llvm_function   = get_script_function(*static_cast<asCScriptFunction*>(pending.function));

		FunctionBuilder builder{context};

		asUINT   length;
		asDWORD* bytecode = pending.function->GetByteCode(&length);

		builder.translate_bytecode(bytecode, length);

		llvm::Function* entry = builder.create_vm_entry_thunk();

		JitSymbol symbol;
		symbol.script_function = pending.function;
		symbol.name            = make_function_name(*pending.function); // TODO: get it from somewhere
		symbol.entry_name      = entry->getName();
		symbol.jit_function    = pending.jit_function;
		m_jit_functions.push_back(symbol);

		m_di_builder->finalize();
	}

	m_pending_functions.clear();
}

void ModuleBuilder::link_symbols()
{
	const auto define_function = [&](auto* address, const std::string& name) {
		if (m_compiler.jit().lookup(name))
		{
			// Symbol already defined
			return;
		}

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
	define_function(runtime::new_script_object, "asllvm.private.new_script_object");
	define_function(runtime::script_vtable_lookup, "asllvm.private.script_vtable_lookup");
	define_function(runtime::system_vtable_lookup, "asllvm.private.system_vtable_lookup");
	define_function(runtime::call_object_method, "asllvm.private.call_object_method");
	define_function(runtime::panic, "asllvm.private.panic");
	define_function(runtime::set_internal_exception, "asllvm.private.set_internal_exception");

	define_function(fmodf, "fmodf");
	define_function(fmod, "fmod");
}
} // namespace asllvm::detail
