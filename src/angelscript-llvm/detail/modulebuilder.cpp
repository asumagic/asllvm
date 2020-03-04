#include <angelscript-llvm/detail/modulebuilder.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <fmt/core.h>

#include <array>

namespace asllvm::detail
{

ModuleBuilder::ModuleBuilder(std::string_view angelscript_module_name) :
	m_module{std::make_unique<llvm::Module>(make_module_name(angelscript_module_name), context)}
{}

FunctionBuilder ModuleBuilder::create_function(asIScriptFunction& function)
{
	std::array<llvm::Type*, 0> types;
	llvm::FunctionType* function_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context), types, false);

	return {
		*this,
		llvm::Function::Create(
			function_type,
			llvm::Function::ExternalLinkage,
			make_function_name(function.GetName(), function.GetNamespace()),
			*m_module.get()
		)
	};
}

void ModuleBuilder::dump_state() const
{
	for (const auto& function : m_module->functions())
	{
		fmt::print(stderr, "Function '{}'\n", function.getName().str());
	}
}

}
