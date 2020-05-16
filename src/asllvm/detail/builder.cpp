#include <asllvm/detail/builder.hpp>

#include <angelscript.h>
#include <asllvm/detail/asinternalheaders.hpp>
#include <asllvm/detail/assert.hpp>
#include <asllvm/detail/jitcompiler.hpp>
#include <asllvm/detail/llvmglobals.hpp>
#include <asllvm/detail/modulecommon.hpp>
#include <asllvm/detail/runtime.hpp>
#include <fmt/core.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

namespace asllvm::detail
{
Builder::Builder(JitCompiler& compiler) :
	m_compiler{compiler},
	m_context{setup_context()},
	m_pass_manager{setup_pass_manager()},
	m_ir_builder{*m_context.getContext()},
	m_types{setup_standard_types()}
{
	llvm::FastMathFlags fast_fp;

	if (m_compiler.config().allow_fast_math)
	{
		fast_fp.set();
	}

	m_ir_builder.setFastMathFlags(fast_fp);
}

llvm::Type* Builder::to_llvm_type(const asCDataType& type) const
{
	if (type.IsPrimitive())
	{
		llvm::Type* base_type = nullptr;

		switch (type.GetTokenType())
		{
		case ttVoid: base_type = m_types.tvoid; break;
		case ttBool: base_type = m_types.i1; break;
		case ttInt8:
		case ttUInt8: base_type = m_types.i8; break;
		case ttInt16:
		case ttUInt16: base_type = m_types.i16; break;
		case ttInt:
		case ttUInt: base_type = m_types.i32; break;
		case ttInt64:
		case ttUInt64: base_type = m_types.i64; break;
		case ttFloat: base_type = m_types.f32; break;
		case ttDouble: base_type = m_types.f64; break;
		default: asllvm_assert(false && "provided primitive type not supported");
		}

		if (type.IsReference())
		{
			return base_type->getPointerTo();
		}

		return base_type;
	}

	if (type.IsReference() || type.IsObjectHandle() || type.IsObject())
	{
		asllvm_assert(type.GetTypeInfo() != nullptr);
		auto&     object_type = static_cast<asCObjectType&>(*type.GetTypeInfo());
		const int type_id     = object_type.GetTypeId();

		if (const auto it = m_object_types.find(type_id); it != m_object_types.end())
		{
			return it->second->getPointerTo();
		}

		llvm::StructType* struct_type = llvm::StructType::create(
			{llvm::ArrayType::get(m_types.i8, type.GetSizeInMemoryBytes())}, object_type.GetName());

		m_object_types.emplace(type_id, struct_type);

		// TODO: what makes most sense between declaring this as non-const and having a non-mutable m_object_types
		// versus this as a const and m_object_types as mutable?
		return struct_type->getPointerTo();
	}

	asllvm_assert(false && "type not supported");
}

StandardTypes Builder::setup_standard_types()
{
	StandardTypes types{};

	auto  context_lock = m_context.getLock();
	auto& context      = *m_context.getContext();

	types.tvoid    = llvm::Type::getVoidTy(context);
	types.i1       = llvm::Type::getInt1Ty(context);
	types.i8       = llvm::Type::getInt8Ty(context);
	types.i16      = llvm::Type::getInt16Ty(context);
	types.i32      = llvm::Type::getInt32Ty(context);
	types.i64      = llvm::Type::getInt64Ty(context);
	types.iptr     = llvm::Type::getInt64Ty(context); // TODO: determine pointer type from target machine
	types.vm_state = types.i8;
	types.f32      = llvm::Type::getFloatTy(context);
	types.f64      = llvm::Type::getDoubleTy(context);

	types.pvoid = types.i8->getPointerTo();
	types.pi8   = types.i8->getPointerTo();
	types.pi16  = types.i16->getPointerTo();
	types.pi32  = types.i32->getPointerTo();
	types.pi64  = types.i64->getPointerTo();
	types.piptr = types.iptr->getPointerTo();
	types.pf32  = types.f32->getPointerTo();
	types.pf64  = types.f64->getPointerTo();

	{
		types.vm_registers = llvm::StructType::create(
			{
				types.pi32,  // programPointer
				types.pi32,  // stackFramePointer
				types.pi32,  // stackPointer
				types.iptr,  // valueRegister
				types.pvoid, // objectRegister
				types.pvoid, // objectType - todo asITypeInfo
				types.i1,    // doProcessSuspend
				types.pvoid, // ctx - todo asIScriptContext
			},
			"asSVMRegisters");
	}

	return types;
}

std::unique_ptr<llvm::LLVMContext> Builder::setup_context() { return std::make_unique<llvm::LLVMContext>(); }

llvm::legacy::PassManager Builder::setup_pass_manager()
{
	constexpr int inlining_threshold = 275;

	llvm::legacy::PassManager pm;
	pm.add(llvm::createVerifierPass());

	if (m_compiler.config().allow_llvm_optimizations)
	{
		llvm::PassManagerBuilder pmb;
		pmb.OptLevel           = 3;
		pmb.Inliner            = llvm::createFunctionInliningPass(inlining_threshold);
		pmb.DisableUnrollLoops = false;
		pmb.LoopVectorize      = true;
		pmb.SLPVectorize       = true;
		pmb.populateModulePassManager(pm);
		pm.add(llvm::createVerifierPass()); // Verify the optimized IR as well
	}

	return pm;
}
} // namespace asllvm::detail
