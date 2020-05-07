#include <angelscript-llvm/detail/codegen/debuginfo.hpp>

namespace asllvm::detail::codegen
{
SourceLocation get_source_location(FunctionContext context, std::size_t bytecode_offset)
{
	int       section;
	const int encoded_line
		= const_cast<asCScriptFunction*>(context.script_function)->GetLineNumber(bytecode_offset, &section);

	const int line = encoded_line & 0xFFFFF, column = encoded_line >> 20;
	return {line, column};
}

llvm::DebugLoc get_debug_location(FunctionContext context, std::size_t bytecode_offset, llvm::DISubprogram* sp)
{
	const SourceLocation loc = get_source_location(context, bytecode_offset);
	return llvm::DebugLoc::get(loc.line, loc.column, sp);
}
} // namespace asllvm::detail::codegen
