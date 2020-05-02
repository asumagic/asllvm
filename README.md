# asllvm

A JIT compiler for AngelScript using LLVM and OrcV2.

**asllvm is not production-ready and many features are not supported yet.**

Note that it is not possible to partially JIT scripts so your application has to be fully supported to work with asllvm.

## Goals

- Improving performance as much as possible.
- Complete AngelScript support.
- Symbol information for profiling tools and source-level debugging (implemented for `gdb` and `perf` already).

## Requirements

You will need:
- A C++17 compliant compiler.
- A recent LLVM version (tested on LLVM9).

Extra dependencies (`{fmt}` and `Catch2`) will be fetched automatically using `hunter`.

## Usage

There is currently no simple usage example, but you can check [`tests/common.cpp`](tests/common.cpp) to get an idea.

You have to register the JIT as usual. In its current state (this should be changed later on), you *need* to call
`JitInterface::BuildModules` before any script call.

## Resources

- [Implementation status](doc/status.md)
- [Performance recommendations when using asllvm](doc/performance.md)
