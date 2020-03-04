#pragma once

#include <llvm/IR/IRBuilder.h>

namespace asllvm::detail
{

class Builder
{
public:
	Builder();

private:
	llvm::IRBuilder<> m_ir_builder;
};

}
