#include <angelscript-llvm/detail/modulebuilder.hpp>

#include <angelscript-llvm/detail/functionbuilder.hpp>
#include <angelscript-llvm/detail/jitcompiler.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <array>
#include <fmt/core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

namespace asllvm::detail
{
ModuleBuilder::ModuleBuilder(JitCompiler& compiler, std::string_view angelscript_module_name) :
	m_compiler{compiler},
	m_module{std::make_unique<llvm::Module>(make_module_name(angelscript_module_name), compiler.builder().context())}
{}

FunctionBuilder ModuleBuilder::create_function(asIScriptFunction& function, asJITFunction& jit_function_output)
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

	const std::string name = make_function_name(function.GetName(), function.GetNamespace());

	llvm::Function* llvm_function
		= llvm::Function::Create(function_type, llvm::Function::InternalLinkage, name, *m_module.get());

	// TODO: fix this, but how to CreateCall with this convention?! in functionbuilder.cpp
	// llvm_function->setCallingConv(llvm::CallingConv::Fast);

	m_jit_functions.emplace_back(name, &jit_function_output);

	return {m_compiler, *this, function, llvm_function};
}

void ModuleBuilder::build()
{
	llvm::PassManagerBuilder pmb;
	pmb.OptLevel           = 3;
	pmb.Inliner            = llvm::createFunctionInliningPass(275);
	pmb.DisableUnrollLoops = false;
	pmb.LoopVectorize      = true;
	pmb.SLPVectorize       = true;

	llvm::legacy::PassManager pm;
	pmb.populateModulePassManager(pm);

	pm.run(*m_module);

	auto jit = ExitOnError(llvm::orc::LLJITBuilder().create());

	if (m_compiler.config().verbose)
	{
		dump_state();
	}

	ExitOnError(
		jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(m_module), m_compiler.builder().extract_old_context())));

	for (auto& pair : m_jit_functions)
	{
		auto entry   = ExitOnError(jit->lookup(pair.first + ".jitentry"));
		*pair.second = reinterpret_cast<asJITFunction>(entry.getAddress());
	}

	// HACK: garbage
	jit.release();
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
