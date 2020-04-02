#include <angelscript-llvm/detail/builder.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript.h>

namespace asllvm::detail
{
Builder::Builder(JitCompiler& compiler) :
	m_compiler{compiler}, m_ir_builder{context}, m_common_definitions{build_common_definitions()}
{}

llvm::Type* Builder::script_type_to_llvm_type(int type_id) const
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

bool Builder::is_script_type_64(int type_id) const
{
	switch (type_id)
	{
	case asTYPEID_INT64:
	case asTYPEID_DOUBLE:
	{
		return true;
	}

	case asTYPEID_VOID:
	case asTYPEID_BOOL:
	case asTYPEID_INT8:
	case asTYPEID_INT16:
	case asTYPEID_INT32:
	{
		return false;
	}

	default: throw std::runtime_error{"type not implemented"};
	}
}

CommonDefinitions Builder::build_common_definitions()
{
	CommonDefinitions definitions;

	auto* boolt = llvm::Type::getInt1Ty(context);
	auto* voidp = llvm::Type::getInt8Ty(context)->getPointerTo();
	auto* dword = llvm::Type::getInt32Ty(context);
	auto* qword = llvm::Type::getInt64Ty(context);

	std::array<llvm::Type*, 8> types{{
		dword->getPointerTo(), // programPointer
		dword->getPointerTo(), // stackFramePointer
		dword->getPointerTo(), // stackPointer
		qword,                 // valueRegister
		voidp,                 // objectRegister
		voidp,                 // objectType - todo asITypeInfo
		boolt,                 // doProcessSuspend
		voidp,                 // ctx - todo asIScriptContext
	}};

	definitions.vm_registers = llvm::StructType::create(types, "asSVMRegisters");

	return definitions;
}
} // namespace asllvm::detail
