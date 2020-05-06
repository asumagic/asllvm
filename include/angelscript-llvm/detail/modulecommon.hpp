#pragma once

#include <angelscript.h>
#include <string>
#include <string_view>

namespace asllvm::detail
{
std::string make_module_name(const asIScriptModule* module);
std::string make_function_name(const asIScriptFunction& function);
std::string make_vm_entry_thunk_name(const asIScriptFunction& function);
std::string make_system_function_name(const asIScriptFunction& function);
std::string make_debug_name(const asIScriptFunction& function);

constexpr asPWORD vtable_userdata_identifier = 0xCAFECAFECAFECAFE;
} // namespace asllvm::detail
