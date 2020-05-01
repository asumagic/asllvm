#pragma once

#include <angelscript.h>
#include <string>
#include <string_view>

namespace asllvm::detail
{
std::string make_module_name(asIScriptModule& module);
std::string make_function_name(asIScriptFunction& function);
std::string make_jit_entry_name(asIScriptFunction& function);
std::string make_system_function_name(asIScriptFunction& function);
std::string make_debug_name(asIScriptFunction& function);

constexpr asPWORD vtable_userdata_identifier = 0xCAFECAFECAFECAFE;
} // namespace asllvm::detail
