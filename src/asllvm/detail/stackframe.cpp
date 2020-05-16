#include <asllvm/detail/stackframe.hpp>

#include <asllvm/detail/assert.hpp>
#include <asllvm/detail/builder.hpp>
#include <asllvm/detail/debuginfo.hpp>
#include <asllvm/detail/jitcompiler.hpp>
#include <asllvm/detail/modulebuilder.hpp>
#include <fmt/core.h>
#include <llvm/IR/DIBuilder.h>

namespace asllvm::detail
{
StackFrame::StackFrame(FunctionContext context) : m_context{context} {}

void StackFrame::setup()
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	m_storage = ir.CreateAlloca(llvm::ArrayType::get(types.i32, total_space()), nullptr, "storage");
	allocate_parameter_storage();

	m_stack_pointer = variable_space();

	emit_debug_info();
}

void StackFrame::finalize() { asllvm_assert(empty_stack()); }

long StackFrame::variable_space() const { return m_context.script_function->scriptData->variableSpace; }

long StackFrame::stack_space() const
{
	// 2 pointers reserved for exception handling, 1 for asBC_ALLOC. See RESERVE_STACK in as_context.cpp.
	constexpr long reserved_space = 2 * AS_PTR_SIZE;

	return m_context.script_function->scriptData->stackNeeded - variable_space() + reserved_space;
}

long StackFrame::total_space() const { return variable_space() + stack_space(); }

StackFrame::AsStackOffset StackFrame::current_stack_pointer() const { return m_stack_pointer; }

void StackFrame::ugly_hack_stack_pointer_within_bounds()
{
	m_stack_pointer = std::max(m_stack_pointer, variable_space());
}

bool StackFrame::empty_stack() const { return m_stack_pointer == variable_space(); }

void StackFrame::check_stack_pointer_bounds()
{
	asllvm_assert(m_stack_pointer >= variable_space());
	asllvm_assert(m_stack_pointer <= total_space());
}

void StackFrame::push(llvm::Value* value, std::size_t dwords)
{
	m_stack_pointer += dwords;
	store(m_stack_pointer, value);
}

void StackFrame::pop(std::size_t dwords) { m_stack_pointer -= dwords; }

llvm::Value* StackFrame::pop(std::size_t dwords, llvm::Type* type)
{
	llvm::Value* value = load(m_stack_pointer, type);
	pop(dwords);

	return value;
}

llvm::Value* StackFrame::top(llvm::Type* type) { return load(m_stack_pointer, type); }

llvm::Value* StackFrame::load(StackFrame::AsStackOffset offset, llvm::Type* type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	return ir.CreateLoad(type, pointer_to(offset, type), fmt::format("local@{}.value", offset));
}

void StackFrame::store(StackFrame::AsStackOffset offset, llvm::Value* value)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	ir.CreateStore(value, pointer_to(offset, value->getType()));
}

llvm::Value* StackFrame::pointer_to(StackFrame::AsStackOffset offset, llvm::Type* pointee_type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	llvm::Value* pointer = pointer_to(offset);
	return ir.CreatePointerCast(pointer, pointee_type->getPointerTo(), fmt::format("local@{}.castedptr", offset));
}

llvm::Value* StackFrame::pointer_to(StackFrame::AsStackOffset offset)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	// Value at stack offset is a parameter
	if (offset <= 0)
	{
		return m_parameters.at(offset).local_alloca;
	}

	// Value at stack offset is within the stack
	const long real_offset = total_space() - offset;

	// Ensure that offset is within alloca boundaries
	// TODO: this should check for the pointee type as well
	asllvm_assert(real_offset >= 0);
	asllvm_assert(real_offset <= total_space());

	return ir.CreateInBoundsGEP(
		m_storage,
		{llvm::ConstantInt::get(types.iptr, 0), llvm::ConstantInt::get(types.iptr, real_offset)},
		fmt::format("local@{}.ptr", offset));
}

llvm::AllocaInst* StackFrame::storage_alloca() { return m_storage; }

void StackFrame::allocate_parameter_storage()
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	asCScriptEngine&   engine  = m_context.compiler->engine();

	AsStackOffset stack_offset = 0;
	auto          argument_it  = m_context.llvm_function->arg_begin();

	const auto allocate_parameter = [&](const asCDataType& data_type, const char* name) -> void {
		Parameter parameter;
		parameter.argument_index = std::distance(m_context.llvm_function->arg_begin(), argument_it);
		parameter.local_alloca   = ir.CreateAlloca(builder.to_llvm_type(data_type), nullptr, name);
		parameter.type_id        = engine.GetTypeIdFromDataType(data_type);
		parameter.debug_name     = name;

		const auto [it, success] = m_parameters.emplace(stack_offset, parameter);
		asllvm_assert(success);

		ir.CreateStore(argument_it, parameter.local_alloca);

		stack_offset -= data_type.GetSizeOnStackDWords();
		++argument_it;
	};

	if (m_context.script_function->returnType.GetTokenType() != ttVoid)
	{
		if (m_context.script_function->DoesReturnOnStack())
		{
			allocate_parameter(m_context.script_function->returnType, "stackRetPtr");
		}
		else
		{
			// Pointer to return value as the first arg
			++argument_it;
		}
	}

	// is method?
	if (m_context.script_function->objectType != nullptr)
	{
		allocate_parameter(engine.GetDataTypeFromTypeId(m_context.script_function->objectType->GetTypeId()), "thisPtr");
	}

	for (std::size_t i = 0; i < m_context.script_function->parameterTypes.GetLength(); ++i)
	{
		allocate_parameter(
			m_context.script_function->parameterTypes[i], &(m_context.script_function->parameterNames[i])[0]);
	}
}

void StackFrame::emit_debug_info()
{
	asCScriptEngine&   engine = m_context.compiler->engine();
	llvm::IRBuilder<>& ir     = m_context.compiler->builder().ir();
	llvm::DIBuilder&   di     = m_context.module_builder->di_builder();

	llvm::DISubprogram* sp = m_context.llvm_function->getSubprogram();

	for (const auto& [stack_offset, param] : m_parameters)
	{
		llvm::DILocalVariable* local = di.createParameterVariable(
			sp,
			param.debug_name,
			param.argument_index,
			sp->getFile(),
			0,
			m_context.module_builder->get_debug_type(param.type_id));

		di.insertDeclare(
			param.local_alloca,
			local,
			di.createExpression(),
			get_debug_location(m_context, 0, sp),
			ir.GetInsertBlock());
	}

	{
		const auto& vars = m_context.script_function->scriptData->variables;
		for (std::size_t i = m_context.script_function->GetParamCount(); i < vars.GetLength(); ++i)
		{
			const auto& var = vars[i];

			llvm::DILocalVariable* local = di.createAutoVariable(
				sp,
				&var->name[0],
				sp->getFile(),
				0,
				m_context.module_builder->get_debug_type(engine.GetTypeIdFromDataType(var->type)));

			di.insertDeclare(
				m_storage,
				local,
				di.createExpression(llvm::ArrayRef<std::int64_t>{
					llvm::dwarf::DW_OP_plus_uconst, std::int64_t(total_space() - var->stackOffset) * 4}),
				get_debug_location(m_context, 0, sp),
				ir.GetInsertBlock());
		}
	}
}
} // namespace asllvm::detail
