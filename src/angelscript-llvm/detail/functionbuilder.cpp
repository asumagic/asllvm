#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <angelscript-llvm/detail/modulebuilder.hpp>

namespace asllvm::detail
{

FunctionBuilder::FunctionBuilder(ModuleBuilder& module_builder, llvm::Function* function) :
	m_module_builder{&module_builder},
	m_function{function}
{}

}
