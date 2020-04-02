#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Error.h>
#include <memory>

namespace asllvm::detail
{
extern std::unique_ptr<llvm::LLVMContext> context;
extern llvm::ExitOnError                  ExitOnErr;
} // namespace asllvm::detail
