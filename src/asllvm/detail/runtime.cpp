#include <asllvm/detail/runtime.hpp>

#include <asllvm/detail/modulecommon.hpp>

namespace asllvm::detail::runtime
{
void* script_vtable_lookup(asCScriptObject* object, asCScriptFunction* function)
{
	auto& object_type = *static_cast<asCObjectType*>(object->GetObjectType());
	return reinterpret_cast<void*>(
		object_type.virtualFunctionTable[function->vfTableIdx]->GetUserData(vtable_userdata_identifier));
}

void* system_vtable_lookup(void* object, asPWORD func)
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

void call_object_method(void* object, asCScriptFunction* function)
{
	// TODO: this is not very efficient: this performs an extra call into AS that is more generic than we require: we
	// know a lot of stuff about the function at JIT time.
	auto& engine = *static_cast<asCScriptEngine*>(function->GetEngine());
	engine.CallObjectMethod(object, function->GetId());
}

void* new_script_object(asCObjectType* object_type)
{
	auto* object = static_cast<asCScriptObject*>(userAlloc(object_type->size));
	ScriptObject_Construct(object_type, object);
	return object;
}
} // namespace asllvm::detail::runtime
