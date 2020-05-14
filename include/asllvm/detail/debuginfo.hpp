#pragma once

#include <asllvm/detail/functioncontext.hpp>
#include <llvm/IR/DebugInfoMetadata.h>

namespace asllvm::detail
{
struct SourceLocation
{
	int line, column;
};

SourceLocation get_source_location(FunctionContext context, std::size_t bytecode_offset = 0);
llvm::DebugLoc get_debug_location(FunctionContext context, std::size_t bytecode_offset, llvm::DISubprogram* sp);
} // namespace asllvm::detail
