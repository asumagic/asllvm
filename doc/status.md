# Implementation status

The following list is somewhat incomplete.

## General

- [x] Generate, build, optimize and execute translated IR
- [x] Source-level debugging\*
- [ ] Complete bytecode support

\*: Still fairly rough, but somewhat functional.

## Planned platform support

Some specific features might be platform specific behavior and may need changes to port asllvm to other platforms, e.g.:
- `CommonDefinitions::iptr` is always 64-bit even on 32-bit platforms. This should be detected as needed and used in
    more places than it currently is.
- There may be unexpected C++ ABI differences between platforms, so generated external calls may be incorrect.

- [x] x86-64
  - [x] Linux
  - [ ] Windows (MinGW)

## Bytecode and feature support

This part is fairly incomplete, but provided to give a general idea:

- [x] Integral arithmetic\*
- [x] Floating-point arithmetic\*
- [x] Variables
  - [x] Globals
- [x] Branching (`if`, `for`, `while`, `switch` statements)
- [x] Script function calls
  - [x] Regular functions
  - [ ] Imported functions
  - [ ] Function pointers
- [x] Application interface
  - [ ] Factories
  - [ ] List constructors
  - [x] Object types
    - [x] Allocation
    - [x] Pass by value
    - [x] Pass by reference
    - [x] Return by value (from system function)
    - [x] Return by value (from script function)
    - [x] Virtual method calls
    - [ ] Composite calls
  - [ ] Reference-counted types
  - [x] Function calls
- [x] Script classes
  - [x] Constructing and destructing script classes
  - [x] Virtual script calls
    - [x] Devirtualization optimization\*\*\*
  - [x] Reference counted types\*\*
- [ ] VM execution status support
  - [ ] Exception on null pointer dereference
  - [ ] Exception on division by zero
  - [ ] Exception on overflow for some specific arithmetic ops
  - [ ] Support VM register introspection in system calls (for debugging, etc.)
  - [ ] VM suspend support

\*: `a ** b` (i.e. `pow`) is not implemented yet for any type.

\*\*: Reference counting through handles is implemented as stubs and don't actually perform any freeing for now.

\*\*\*: Implemented for trivial cases (method was originally declared as `final`).

**Due to the design of asllvm, it is not possible to partially JIT modules, or to skip JITting for specific modules.**
The JIT assumes that only supported features are used by your scripts and application interface, or it will yield an
assertion failure or broken codegen. Any script call from JIT'd code *must* point to a JIT'd function.

# Comparison, rationale and implementation status

This project was partly created for learning purposes.

An existing AngelScript JIT compiler from BlindMindStudios exists. I believe that asllvm may have several significant
differences with BMS's JIT compiler.

## Source-level debugging

asllvm currently has (limited) support for source-level debugging using gdb. This means that you can debug an
AngelScript script in your IDE - inspect variables, step through, setup breakpoints, etc. with a consistent call stack.

## Cross-platform support

BMS's JIT compiler was made with x86/x86-64 in mind, and cannot be ported to other architectures without a massive
rewrite.

asllvm is currently only implementing x86-64 Linux gcc support, but porting it to both x86 and x86-64 for Windows, Linux
and macOS should not be complicated.

x86-64 Linux and x86-64 Windows MinGW support is planned.

Porting asllvm to other architectures is non-trivial, but feasible, but may require some trickery and debugging around
the ABI support.

## Performance

BMS's JIT compiler has good compile times, potentially better than asllvm, but the generated code is likely to be
less efficient for several reasons:
- BMS's JIT compiler does not really perform any optimization, whereas LLVM does (and does so quite well).
- BMS's JIT compiler fallbacks to the VM in many occasions. asllvm aims to not ever require to use the AS VM as a
    fallback.
- There are some optimizations potentially planned for asllvm that may simplify the generated logic.

Note, however, that asllvm is a *much* larger dependency as it depends on LLVM. Think several tens of megabytes.
