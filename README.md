# angelscript-llvm

A platform-independent JIT compiler for AngelScript.

## Current status

angelscript-llvm is currently highly experimental and will *not* work with your current application, as feature support
is currently very limited.

Right now, only x86-64 on Linux is tested and supported, but when complete, pretty much every LLVM-supported platform
should be able to work.

The [sample script.as](samples/simple/workdir/script.as) (which, at this point, is just a file to test and mess around)
should roughly show what is currently working.

## Requirements

You will need:
- A C++17 compliant compiler.
- A recent LLVM version (tested on LLVM9).
- The `{fmt}` library. Note that eventually, this will be fetched automatically on build.

## Example

An example is available in the `samples/simple` directory.
angelscript-llvm registers using AngelScript's JIT interface and should be straightforward to get working.
