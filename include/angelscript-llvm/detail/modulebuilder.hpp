#pragma once

#include <llvm/IR/Module.h>

#include <memory>

namespace asllvm::detail
{

class ModuleBuilder
{
public:
	ModuleBuilder(const char* name);

private:
	llvm::Module module;
};

}
