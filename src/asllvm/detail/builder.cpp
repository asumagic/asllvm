#include <asllvm/detail/builder.hpp>

#include <asllvm/detail/asinternalheaders.hpp>
#include <asllvm/detail/assert.hpp>
#include <asllvm/detail/jitcompiler.hpp>
#include <asllvm/detail/llvmglobals.hpp>
#include <asllvm/detail/modulecommon.hpp>
#include <asllvm/detail/runtime.hpp>
#include <angelscript.h>
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
	m_defs{setup_common_definitions()}
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
		case ttVoid: base_type = m_defs.tvoid; break;
		case ttBool: base_type = m_defs.i1; break;
		case ttInt8:
		case ttUInt8: base_type = m_defs.i8; break;
		case ttInt16:
		case ttUInt16: base_type = m_defs.i16; break;
		case ttInt:
		case ttUInt: base_type = m_defs.i32; break;
		case ttInt64:
		case ttUInt64: base_type = m_defs.i64; break;
		case ttFloat: base_type = m_defs.f32; break;
		case ttDouble: base_type = m_defs.f64; break;
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

		std::array<llvm::Type*, 1> types{{llvm::ArrayType::get(m_defs.i8, type.GetSizeInMemoryBytes())}};

		llvm::StructType* struct_type = llvm::StructType::create(types, object_type.GetName());
		m_object_types.emplace(type_id, struct_type);

		// TODO: what makes most sense between declaring this as non-const and having a non-mutable m_object_types
		// versus this as a const and m_object_types as mutable?
		return struct_type->getPointerTo();
	}

	asllvm_assert(false && "type not supported");
}

CommonDefinitions Builder::setup_common_definitions()
{
	CommonDefinitions defs{};

	auto  context_lock = m_context.getLock();
	auto& context      = *m_context.getContext();

	defs.tvoid = llvm::Type::getVoidTy(context);
	defs.i1    = llvm::Type::getInt1Ty(context);
	defs.i8    = llvm::Type::getInt8Ty(context);
	defs.i16   = llvm::Type::getInt16Ty(context);
	defs.i32   = llvm::Type::getInt32Ty(context);
	defs.i64   = llvm::Type::getInt64Ty(context);
	defs.iptr  = llvm::Type::getInt64Ty(context); // TODO: determine pointer type from target machine
	defs.f32   = llvm::Type::getFloatTy(context);
	defs.f64   = llvm::Type::getDoubleTy(context);

	defs.pvoid = defs.i8->getPointerTo();
	defs.pi8   = defs.i8->getPointerTo();
	defs.pi16  = defs.i16->getPointerTo();
	defs.pi32  = defs.i32->getPointerTo();
	defs.pi64  = defs.i64->getPointerTo();
	defs.piptr = defs.iptr->getPointerTo();
	defs.pf32  = defs.f32->getPointerTo();
	defs.pf64  = defs.f64->getPointerTo();

	{
		std::array<llvm::Type*, 8> types{
			defs.pi32,  // programPointer
			defs.pi32,  // stackFramePointer
			defs.pi32,  // stackPointer
			defs.iptr,  // valueRegister
			defs.pvoid, // objectRegister
			defs.pvoid, // objectType - todo asITypeInfo
			defs.i1,    // doProcessSuspend
			defs.pvoid, // ctx - todo asIScriptContext
		};

		defs.vm_registers = llvm::StructType::create(types, "asSVMRegisters");
	}

	return defs;
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
