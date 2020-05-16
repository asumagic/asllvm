#pragma once

#include <asllvm/detail/asinternalheaders.hpp>
#include <cstdint>

namespace asllvm::detail::runtime
{
void*             script_vtable_lookup(asCScriptObject* object, asCScriptFunction* function);
void*             system_vtable_lookup(void* object, asPWORD func);
void              call_object_method(void* object, asCScriptFunction* function);
void*             new_script_object(asCObjectType* object_type);
[[noreturn]] void panic();
} // namespace asllvm::detail::runtime
