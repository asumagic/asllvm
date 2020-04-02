#include <angelscript-llvm/detail/llvmglobals.hpp>

namespace asllvm::detail
{
std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
llvm::ExitOnError                  ExitOnErr;
} // namespace asllvm::detail
