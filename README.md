# asllvm

A platform-independent JIT compiler for AngelScript.

## Current status

asllvm is currently highly experimental **and will *not* work with your current application** as feature support
is currently very limited.

Right now, only x86-64 on Linux is tested and supported, but when complete, pretty much every LLVM-supported platform
should be able to work.

The tests scattered around `.cpp` and `.as` files in the [`tests/`](tests/) directory should provide an idea of the
current support.

## Comparison

An existing AngelScript from BlindMindStudios exist. Here are some differences you should expect.

- asllvm should be significantly easier to port to non-x86 CPU architectures and/or other OSes.
- asllvm will likely be slower at compiling, but may generate significantly more efficient code. There are two reasons
for this. First, asllvm aims to support all of the AngelScript bytecode, whereas BlindMindStudios' JIT fallbacks to the
AngelScript interpreter for various cases. Second, LLVM is able to perform a lot of optimizations over the generated
code.
- LLVM is a heavy dependency. Adding asllvm to your application will likely bloat it up by a few tens of megabytes.

## Requirements

You will need:
- A C++17 compliant compiler.
- A recent LLVM version (tested on LLVM9).

Extra dependencies (`{fmt}` and `Catch2`) will be fetched automatically using `hunter`.

## Example

An example is available in the `samples/simple` directory.
asllvm registers using AngelScript's JIT interface and should be straightforward to get working.
