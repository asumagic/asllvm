# Compatibility-breaking behavior

For your application to work properly, it must follow these requirements.

## All modules must be compiled by `asllvm`

`asllvm` is not able to partially compile modules or compile modules optionally:
- **All** functions within a module should be supported (see [status](status.md)).
- **All** modules in your application should be compiled with JIT.

## Do not inspect the context state

`asllvm` does not use the internal VM structures. Things like the callstack, context variables and parameters provided
by AngelScript cannot be relied upon and will likely yield undefined behavior when used.

There are however a few specific exceptions to this rule:
- `asIScriptContext->m_callingSystemFunction` is populated before system calls.

In general, this means that when the JIT is enabled, most things you would do with `asGetActiveContext()` cannot be
used - e.g. the debugging add-on (note that `gdb` source-level debugging is an alternative for this usecase).
