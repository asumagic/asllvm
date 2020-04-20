# asllvm

A JIT compiler for AngelScript.

## Current status

asllvm is currently highly experimental **and will *not* work with your current application** as feature support
is currently very limited.

The tests scattered around `.cpp` and `.as` files in the [`tests/`](tests/) directory should provide an idea of the
current support.

## Comparison, rationale and implementation status

This project was partly created for learning purposes.

An existing AngelScript JIT compiler from BlindMindStudios exists. I believe that asllvm may have several significant
differences with BMS's JIT compiler.

### Cross-platform support

BMS's JIT compiler was made with x86/x86-64 in mind, and cannot be ported to other architectures without a massive
rewrite.
asllvm is currently only implementing x86-64 Linux gcc support, but porting it to both x86 and x86-64 for Windows, Linux
and macOS should not be complicated.
x86-64 Linux and x86-64 Windows MinGW support is planned.
Porting asllvm to other architectures is non-trivial, but feasible, but may require some trickery and debugging around
the ABI support.

### Performance

BMS's JIT compiler has good compile times, potentially better than asllvm, but the generated code is likely to be
less efficient for several reasons:
- BMS's JIT compiler does not really perform any optimization, whereas LLVM does (and does so quite well).
- BMS's JIT compiler fallbacks to the VM in many occasions. asllvm aims to not ever require to use the AS VM as a
    fallback.
- There are some optimizations potentially planned for asllvm that may simplify the generated logic.

Note, however, that asllvm is a *much* larger dependency as it depends on LLVM. Think several tens of megabytes.

## Requirements

You will need:
- A C++17 compliant compiler.
- A recent LLVM version (tested on LLVM9).

Extra dependencies (`{fmt}` and `Catch2`) will be fetched automatically using `hunter`.

## Usage

There is currently no simple usage example, but you can check [`tests/common.cpp`](tests/common.cpp) to get an idea.
You have to register the JIT as usual. In its current state (this should be changed later on), you *need* to call
`JitInterface::BuildModules` before any script call.
