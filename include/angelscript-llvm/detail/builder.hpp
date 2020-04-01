#pragma once

#include <llvm/IR/IRBuilder.h>

namespace asllvm::detail
{

class Builder
{
public:
	Builder();

	llvm::IRBuilder<>& ir_builder()
	{
		return m_ir_builder;
	}

private:
	llvm::IRBuilder<> m_ir_builder;
};

}
