# asllvm

A platform-independent JIT compiler for AngelScript.

## Current status

asllvm is currently highly experimental and will *not* work with your current application, as feature support
is currently very limited.

Right now, only x86-64 on Linux is tested and supported, but when complete, pretty much every LLVM-supported platform
should be able to work.

The tests scattered around `.cpp` and `.as` files in the [`tests/`](tests/) directory should provide an idea of the
current support.

## Requirements

You will need:
- A C++17 compliant compiler.
- A recent LLVM version (tested on LLVM9).
- The `{fmt}` library. Note that eventually, this will be fetched automatically on build.

## Example

An example is available in the `samples/simple` directory.
asllvm registers using AngelScript's JIT interface and should be straightforward to get working.
