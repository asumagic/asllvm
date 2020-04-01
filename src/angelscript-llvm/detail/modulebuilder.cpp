#include <angelscript-llvm/detail/modulebuilder.hpp>

#include <angelscript-llvm/detail/functionbuilder.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <angelscript-llvm/jit.hpp>

#include <fmt/core.h>

#include <array>

namespace asllvm::detail
{
ModuleBuilder::ModuleBuilder(JitCompiler& compiler, std::string_view angelscript_module_name) :
	m_compiler{compiler}, m_module{std::make_unique<llvm::Module>(make_module_name(angelscript_module_name), context)}
{}

FunctionBuilder ModuleBuilder::create_function(asIScriptFunction& function)
{
	std::vector<llvm::Type*> types;

	std::size_t parameter_count = function.GetParamCount();
	for (std::size_t i = 0; i < parameter_count; ++i)
	{
		int type_id = 0;
		function.GetParam(i, &type_id);

		types.push_back(m_compiler.builder().script_type_to_llvm_type(type_id));
	}

	llvm::Type* return_type = m_compiler.builder().script_type_to_llvm_type(function.GetReturnTypeId());

	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, types, false);

	llvm::Function* llvm_function = llvm::Function::Create(
		function_type,
		llvm::Function::InternalLinkage,
		make_function_name(function.GetName(), function.GetNamespace()),
		*m_module.get());

	llvm_function->setCallingConv(llvm::CallingConv::Fast);

	return {m_compiler, *this, function, llvm_function};
}

void ModuleBuilder::dump_state() const
{
	for (const auto& function : m_module->functions())
	{
		fmt::print(stderr, "Function '{}'\n", function.getName().str());
	}

	m_module->print(llvm::errs(), nullptr);
}

} // namespace asllvm::detail
