#include <angelscript-llvm/detail/builder.hpp>

#include <angelscript-llvm/detail/llvmglobals.hpp>

namespace asllvm::detail
{

Builder::Builder() :
	m_ir_builder{context}
{}

}
