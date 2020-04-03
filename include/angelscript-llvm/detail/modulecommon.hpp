#pragma once

#include <angelscript.h>
#include <string>
#include <string_view>

namespace asllvm::detail
{
std::string make_module_name(std::string_view angelscript_module_name);
std::string make_function_name(std::string_view angelscript_function_name, std::string_view namespace_name = {});
std::string make_jit_entry_name(std::string_view angelscript_function_name, std::string_view namespace_name = {});
std::string make_system_function_name(asIScriptFunction& function);
} // namespace asllvm::detail
