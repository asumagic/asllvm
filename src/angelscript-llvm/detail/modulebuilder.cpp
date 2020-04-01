#include <angelscript-llvm/detail/modulebuilder.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <fmt/core.h>

#include <array>

namespace asllvm::detail
{

ModuleBuilder::ModuleBuilder(Builder& builder, std::string_view angelscript_module_name) :
	m_builder{builder},
	m_module{std::make_unique<llvm::Module>(make_module_name(angelscript_module_name), context)}
{}

FunctionBuilder ModuleBuilder::create_function(asIScriptFunction& function)
{
	std::vector<llvm::Type*> types;

	std::size_t parameter_count = function.GetParamCount();
	for (std::size_t i = 0; i < parameter_count; ++i)
	{
		int type_id = 0;
		function.GetParam(i, &type_id);

		types.push_back(llvm_type(type_id));
	}

	llvm::Type* return_type = llvm_type(function.GetReturnTypeId());

	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, types, false);

	llvm::Function* llvm_function = llvm::Function::Create(
		function_type,
		llvm::Function::InternalLinkage,
		make_function_name(function.GetName(), function.GetNamespace()),
		*m_module.get()
	);

	llvm_function->setCallingConv(llvm::CallingConv::Fast);

	return {
		m_builder,
		*this,
		llvm_function
	};
}

void ModuleBuilder::dump_state() const
{
	for (const auto& function : m_module->functions())
	{
		fmt::print(stderr, "Function '{}'\n", function.getName().str());
	}

	m_module->print(llvm::errs(), nullptr);
}

llvm::Type* ModuleBuilder::llvm_type(int type_id)
{
	switch (type_id)
	{
	case asTYPEID_VOID: return llvm::Type::getVoidTy(context);
	case asTYPEID_BOOL: return llvm::Type::getInt1Ty(context);
	case asTYPEID_INT8: return llvm::Type::getInt8Ty(context);
	case asTYPEID_INT16: return llvm::Type::getInt16Ty(context);
	case asTYPEID_INT32: return llvm::Type::getInt32Ty(context);
	case asTYPEID_INT64: return llvm::Type::getInt64Ty(context);
	default: throw std::runtime_error{"type not implemented"};
	}
}

}
