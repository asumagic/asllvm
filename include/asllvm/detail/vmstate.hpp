#pragma once

#include <cstdint>

namespace asllvm::detail
{
enum class VmState : std::uint8_t
{
	Ok = 0,
	ExceptionExternal,
	ExceptionNullPointer
};
}
