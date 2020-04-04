#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <angelscript-llvm/detail/jitcompiler.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>

#include <array>
#include <fmt/core.h>

// Include order matters apparently (probably an AS bug: as_memory.h seems to be required by as_array.h)
// clang-format off
#include <as_memory.h>
#include <as_callfunc.h>
#include <as_scriptfunction.h>
// clang-format on

namespace asllvm::detail
{
FunctionBuilder::FunctionBuilder(
	JitCompiler&       compiler,
	ModuleBuilder&     module_builder,
	asIScriptFunction& script_function,
	llvm::Function*    llvm_function) :
	m_compiler{compiler},
	m_module_builder{module_builder},
	m_script_function{script_function},
	m_llvm_function{llvm_function},
	m_entry_block{llvm::BasicBlock::Create(m_compiler.builder().context(), "entry", llvm_function)}
{
	m_compiler.builder().ir().SetInsertPoint(m_entry_block);
}

llvm::Function* FunctionBuilder::read_bytecode(asDWORD* bytecode, asUINT length)
{
	if (m_script_function.GetParamCount() != 0)
	{
		int type_id;
		m_script_function.GetParam(0, &type_id);
		m_locals_offset = m_compiler.builder().get_script_type_dword_size(type_id);
	}

	const auto walk_bytecode = [&](auto&& func) {
		asDWORD* bytecode_current = bytecode;
		asDWORD* bytecode_end     = bytecode + length;

		while (bytecode_current < bytecode_end)
		{
			const asSBCInfo&  info             = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode_current)];
			const std::size_t instruction_size = asBCTypeSize[info.type];

			InstructionContext context;
			context.pointer = bytecode_current;
			context.info    = &info;
			context.offset  = std::distance(bytecode, bytecode_current);

			func(context);

			bytecode_current += instruction_size;
		}
	};

	try
	{
		walk_bytecode([this](InstructionContext instruction) { return preprocess_instruction(instruction); });

		{
			llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
			llvm::LLVMContext& context = m_compiler.builder().context();

			m_locals = ir.CreateAlloca(
				llvm::ArrayType::get(llvm::IntegerType::getInt32Ty(context), m_locals_size + m_max_extra_stack_size),
				0,
				"locals");
			m_value = ir.CreateAlloca(llvm::IntegerType::getInt64Ty(context), 0, "valuereg");

			m_stack_pointer = m_locals_size + m_max_extra_stack_size;
		}

		walk_bytecode([this](InstructionContext instruction) { return read_instruction(instruction); });
	}
	catch (std::exception& exception)
	{
		m_llvm_function->removeFromParent();
		throw;
	}

	return m_llvm_function;
}

llvm::Function* FunctionBuilder::create_wrapper_function()
{
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
	llvm::LLVMContext& context = m_compiler.builder().context();
	CommonDefinitions& defs    = m_compiler.builder().definitions();

	const std::array<llvm::Type*, 2> types{defs.vm_registers->getPointerTo(), defs.i64};

	llvm::Type* return_type = defs.tvoid;

	llvm::Function* wrapper_function = llvm::Function::Create(
		llvm::FunctionType::get(return_type, types, false),
		llvm::Function::ExternalLinkage,
		make_function_name(m_script_function.GetName(), m_script_function.GetNamespace()) + ".jitentry",
		m_module_builder.module());

	llvm::BasicBlock* block = llvm::BasicBlock::Create(context, "entry", wrapper_function);

	ir.SetInsertPoint(block);

	llvm::Argument* registers = &*(wrapper_function->arg_begin() + 0);
	registers->setName("vmregs");

	llvm::Argument* arg = &*(wrapper_function->arg_begin() + 1);
	arg->setName("jitarg");

	llvm::Value* fp = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(defs.i64, 0), llvm::ConstantInt::get(defs.i32, 1)};

		auto* pointer = ir.CreateGEP(registers, indices);
		return ir.CreateLoad(llvm::Type::getInt32Ty(context)->getPointerTo(), pointer, "stackFramePointer");
	}();

	std::array<llvm::Value*, 1> args{{fp}};

	llvm::Value* ret = ir.CreateCall(m_llvm_function, args);

	if (m_llvm_function->getReturnType() != llvm::Type::getVoidTy(context))
	{
		llvm::Value* value_register = [&] {
			std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(defs.i64, 0),
												llvm::ConstantInt::get(defs.i32, 3)};

			return ir.CreateGEP(registers, indices, "valueRegister");
		}();

		ir.CreateStore(ret, ir.CreateBitCast(value_register, ret->getType()->getPointerTo()));
	}

	llvm::Value* pp = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(defs.i64, 0), llvm::ConstantInt::get(defs.i32, 0)};

		return ir.CreateGEP(registers, indices, "programPointer");
	}();

	// Set the program pointer to the RET instruction
	auto* ret_ptr_value = ir.CreateIntToPtr(
		llvm::ConstantInt::get(defs.i64, reinterpret_cast<std::uintptr_t>(m_ret_pointer)), defs.pi32);
	ir.CreateStore(ret_ptr_value, pp);

	ir.CreateRetVoid();

	return wrapper_function;
}

void FunctionBuilder::preprocess_instruction(InstructionContext instruction)
{
	switch (instruction.info->bc)
	{
	// These instructions do not write to the stack
	case asBC_JitEntry:
	case asBC_SUSPEND:
	case asBC_CpyVtoR4:
	case asBC_CpyVtoR8:
	case asBC_RET:
	case asBC_CALLSYS:
	case asBC_CALL:
	case asBC_CMPi:
	case asBC_CMPIi:
	{
		break;
	}

	// These instructions write to the stack, always at the first sword
	case asBC_ADDi:
	case asBC_ADDi64:
	case asBC_SUBIi:
	case asBC_CpyVtoV4:
	case asBC_CpyVtoV8:
	case asBC_CpyRtoV4:
	case asBC_CpyRtoV8:
	case asBC_sbTOi:
	case asBC_swTOi:
	case asBC_ubTOi:
	case asBC_uwTOi:
	case asBC_iTOb:
	case asBC_iTOw:
	case asBC_i64TOi:
	case asBC_uTOi64:
	case asBC_iTOi64:
	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:
	{
		auto target   = asBC_SWORDARG0(instruction.pointer);
		m_locals_size = std::max(m_locals_size, long(target) + 2); // TODO: pretty dodgy
		break;
	}

	// These instructions always write a pointer to the stack
	case asBC_PGA:
	case asBC_PSF:
	{
		m_max_extra_stack_size += AS_PTR_SIZE;
		break;
	}

	// These instructions always write a 32-bit value to the stack
	case asBC_PshV4:
	case asBC_PshC4:
	{
		++m_max_extra_stack_size;
		break;
	}

	// These instructions always write a 64-bit value to the stack
	case asBC_PshV8:
	case asBC_PshC8:
	{
		m_max_extra_stack_size += 2;
		break;
	}

	// Inconditional jump
	case asBC_JMP:
	{
		preprocess_unconditional_branch(instruction);
		break;
	}

	// Conditional jump instructions
	case asBC_JZ:
	case asBC_JNZ:
	case asBC_JS:
	case asBC_JNS:
	case asBC_JP:
	case asBC_JNP:
	{
		preprocess_conditional_branch(instruction);
		break;
	}

	default:
	{
		throw std::runtime_error{
			fmt::format("cannot preprocess unrecognized instruction '{}'", instruction.info->name)};
	}
	}
}

void FunctionBuilder::read_instruction(InstructionContext instruction)
{
	asIScriptEngine&   engine  = m_compiler.engine();
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
	llvm::LLVMContext& context = m_compiler.builder().context();
	CommonDefinitions& defs    = m_compiler.builder().definitions();

	if (auto it = m_jump_map.find(instruction.offset); it != m_jump_map.end())
	{
		emit_branch_if_missing(it->second);
	}

	switch (instruction.info->bc)
	{
	case asBC_JitEntry:
	{
		if (m_compiler.config().verbose)
		{
			m_compiler.diagnostic(engine, "Found JIT entry point, patching as valid entry point");
		}

		// Pass the JitCompiler as the jitArg value, which can be used by lazy_jit_compiler().
		// TODO: this is probably UB
		asBC_PTRARG(instruction.pointer) = reinterpret_cast<asPWORD>(&m_compiler);

		break;
	}

	case asBC_SUSPEND:
	{
		if (m_compiler.config().verbose)
		{
			m_compiler.diagnostic(engine, "Found VM suspend, these are unsupported and ignored", asMSGTYPE_WARNING);
		}

		break;
	}

	case asBC_ADDi:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Add, defs.i32);
		break;
	}

	case asBC_ADDi64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Add, defs.i64);
		break;
	}

	case asBC_SUBIi:
	{
		emit_stack_arithmetic_imm(instruction, llvm::Instruction::Sub, defs.i32);
		break;
	}

	case asBC_CpyVtoV4:
	{
		auto target = asBC_SWORDARG0(instruction.pointer);
		auto source = asBC_SWORDARG1(instruction.pointer);

		store_stack_value(target, load_stack_value(source, defs.i32));

		break;
	}

	case asBC_CpyVtoV8:
	{
		auto target = asBC_SWORDARG0(instruction.pointer);
		auto source = asBC_SWORDARG1(instruction.pointer);

		store_stack_value(target, load_stack_value(source, defs.i64));

		break;
	}

	case asBC_CpyVtoR4:
	{
		store_return_register_value(load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32));
		break;
	}

	case asBC_CpyVtoR8:
	{
		store_return_register_value(load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i64));
		break;
	}

	case asBC_CpyRtoV4:
	{
		store_stack_value(asBC_SWORDARG0(instruction.pointer), load_return_register_value(defs.i32));
		break;
	}

	case asBC_CpyRtoV8:
	{
		store_stack_value(asBC_SWORDARG0(instruction.pointer), load_return_register_value(defs.i64));
		break;
	}

	case asBC_sbTOi: emit_stack_integer_sign_extend(instruction, defs.i8, defs.i32); break;
	case asBC_swTOi: emit_stack_integer_sign_extend(instruction, defs.i16, defs.i32); break;
	case asBC_ubTOi: emit_stack_integer_zero_extend(instruction, defs.i8, defs.i32); break;
	case asBC_uwTOi: emit_stack_integer_zero_extend(instruction, defs.i16, defs.i32); break;
	case asBC_iTOb: emit_stack_integer_trunc(instruction, defs.i32, defs.i8); break;
	case asBC_iTOw: emit_stack_integer_trunc(instruction, defs.i32, defs.i16); break;
	case asBC_i64TOi: emit_stack_integer_trunc(instruction, defs.i64, defs.i32); break;
	case asBC_uTOi64: emit_stack_integer_zero_extend(instruction, defs.i32, defs.i64); break;
	case asBC_iTOi64: emit_stack_integer_sign_extend(instruction, defs.i32, defs.i64); break;

	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:
	{
		store_stack_value(
			asBC_SWORDARG0(instruction.pointer), llvm::ConstantInt::get(defs.i32, asBC_DWORDARG(instruction.pointer)));

		break;
	}

	case asBC_PGA:
	{
		m_stack_pointer -= AS_PTR_SIZE;
		store_stack_value(m_stack_pointer, llvm::ConstantInt::get(defs.i64, asBC_PTRARG(instruction.pointer)));
		break;
	}

	case asBC_PSF:
	{
		m_stack_pointer -= AS_PTR_SIZE;
		store_stack_value(m_stack_pointer, load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i64));
		break;
	}

	case asBC_PshV4:
	{
		--m_stack_pointer;
		store_stack_value(m_stack_pointer, load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32));
		break;
	}

	case asBC_PshV8:
	{
		m_stack_pointer -= 2;
		store_stack_value(m_stack_pointer, load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i64));
		break;
	}

	case asBC_PshC4:
	{
		--m_stack_pointer;
		store_stack_value(m_stack_pointer, llvm::ConstantInt::get(defs.i32, asBC_DWORDARG(instruction.pointer)));
		break;
	}

	case asBC_PshC8:
	{
		m_stack_pointer -= 2;
		store_stack_value(m_stack_pointer, llvm::ConstantInt::get(defs.i64, asBC_QWORDARG(instruction.pointer)));
		break;
	}

	case asBC_CMPi:
	{
		llvm::Value* lhs = load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32);
		llvm::Value* rhs = load_stack_value(asBC_SWORDARG1(instruction.pointer), defs.i32);
		emit_integral_compare(lhs, rhs);
		break;
	}

	case asBC_CMPIi:
	{
		llvm::Value* lhs = load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32);
		llvm::Value* rhs = llvm::ConstantInt::get(defs.i32, asBC_INTARG(instruction.pointer));
		emit_integral_compare(lhs, rhs);
		break;
	}

	case asBC_CALLSYS:
	{
		asIScriptFunction* function = engine.GetFunctionById(asBC_INTARG(instruction.pointer));

		if (function == nullptr)
		{
			throw std::runtime_error{"function referenced in CALLSYS unexpectedly null"};
		}

		emit_system_call(*function);
		break;
	}

	case asBC_CALL:
	{
		asIScriptFunction* function = engine.GetFunctionById(asBC_INTARG(instruction.pointer));

		llvm::Value*                new_frame_pointer = get_stack_value_pointer(m_stack_pointer, defs.i32);
		std::array<llvm::Value*, 1> args{{new_frame_pointer}};

		llvm::Function* callee = m_module_builder.create_function(*function);

		llvm::Value* ret = ir.CreateCall(callee->getFunctionType(), callee, args);

		if (callee->getReturnType() != llvm::Type::getVoidTy(context))
		{
			// Store to the value register
			llvm::Value* typed_value_register = ir.CreateBitCast(m_value, ret->getType()->getPointerTo());
			ir.CreateStore(ret, typed_value_register);
		}

		m_stack_pointer += static_cast<asCScriptFunction&>(*function).GetSpaceNeededForArguments();

		break;
	}

	case asBC_RET:
	{
		if (m_llvm_function->getReturnType() == defs.tvoid)
		{
			ir.CreateRetVoid();
		}
		else
		{
			ir.CreateRet(load_return_register_value(m_llvm_function->getReturnType()));
		}

		m_ret_pointer = instruction.pointer;
		break;
	}

	case asBC_JMP:
	{
		ir.CreateBr(get_branch_target(instruction));
		break;
	}

	case asBC_JNS:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_SGE, load_return_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

		ir.CreateCondBr(condition, get_branch_target(instruction), get_conditional_fail_branch_target(instruction));

		break;
	}

	default:
	{
		throw std::runtime_error{fmt::format("could not recognize bytecode instruction '{}'", instruction.info->name)};
	}
	}
}

void FunctionBuilder::emit_stack_integer_trunc(
	FunctionBuilder::InstructionContext instruction, llvm::Type* source, llvm::Type* destination)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	auto stack_offset = (source == defs.i64 || destination == defs.i64) ? asBC_SWORDARG1(instruction.pointer)
																		: asBC_SWORDARG0(instruction.pointer);

	llvm::Value* original  = load_stack_value(stack_offset, source);
	llvm::Value* truncated = ir.CreateTrunc(original, destination);

	store_stack_value(asBC_SWORDARG0(instruction.pointer), truncated);
}

void FunctionBuilder::emit_stack_integer_sign_extend(
	FunctionBuilder::InstructionContext instruction, llvm::Type* source, llvm::Type* destination)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	auto stack_offset = (source == defs.i64 || destination == defs.i64) ? asBC_SWORDARG1(instruction.pointer)
																		: asBC_SWORDARG0(instruction.pointer);

	llvm::Value* original  = load_stack_value(asBC_SWORDARG0(instruction.pointer), source);
	llvm::Value* truncated = ir.CreateSExt(original, destination);

	store_stack_value(asBC_SWORDARG0(instruction.pointer), truncated);
}

void FunctionBuilder::emit_stack_integer_zero_extend(
	FunctionBuilder::InstructionContext instruction, llvm::Type* source, llvm::Type* destination)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	auto stack_offset = (source == defs.i64 || destination == defs.i64) ? asBC_SWORDARG1(instruction.pointer)
																		: asBC_SWORDARG0(instruction.pointer);

	llvm::Value* original  = load_stack_value(asBC_SWORDARG0(instruction.pointer), source);
	llvm::Value* truncated = ir.CreateZExt(original, destination);

	store_stack_value(asBC_SWORDARG0(instruction.pointer), truncated);
}

void FunctionBuilder::emit_stack_arithmetic(
	InstructionContext instruction, llvm::Instruction::BinaryOps op, llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	llvm::Value* lhs    = load_stack_value(asBC_SWORDARG1(instruction.pointer), type);
	llvm::Value* rhs    = load_stack_value(asBC_SWORDARG2(instruction.pointer), type);
	llvm::Value* result = ir.CreateBinOp(op, lhs, rhs);
	store_stack_value(asBC_SWORDARG0(instruction.pointer), result);
}

void FunctionBuilder::emit_stack_arithmetic_imm(
	FunctionBuilder::InstructionContext instruction, llvm::Instruction::BinaryOps op, llvm::Type* type)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	llvm::Value* lhs    = load_stack_value(asBC_SWORDARG1(instruction.pointer), type);
	llvm::Value* rhs    = llvm::ConstantInt::get(defs.i32, asBC_INTARG(instruction.pointer + 1));
	llvm::Value* result = ir.CreateBinOp(op, lhs, rhs);
	store_stack_value(asBC_SWORDARG0(instruction.pointer), result);
}

void FunctionBuilder::emit_integral_compare(llvm::Value* lhs, llvm::Value* rhs)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	llvm::Value* constant_lt = llvm::ConstantInt::get(defs.i32, -1);
	llvm::Value* constant_eq = llvm::ConstantInt::get(defs.i32, 0);
	llvm::Value* constant_gt = llvm::ConstantInt::get(defs.i32, 1);

	llvm::Value* lower_than   = ir.CreateICmp(llvm::CmpInst::ICMP_SLT, lhs, rhs);
	llvm::Value* greater_than = ir.CreateICmp(llvm::CmpInst::ICMP_SGT, lhs, rhs);

	// lt_or_eq = lhs < rhs ? -1 : 0;
	llvm::Value* lt_or_eq = ir.CreateSelect(lower_than, constant_lt, constant_eq);

	// cmp = lhs > rhs ? 1 : lt_or_eq
	// cmp = lhs > rhs ? 1 : (lhs < rhs ? -1 : 0)
	llvm::Value* cmp = ir.CreateSelect(greater_than, constant_gt, lt_or_eq);

	store_return_register_value(cmp);
}

void FunctionBuilder::emit_system_call(asIScriptFunction& function)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	llvm::Function* callee = m_module_builder.get_system_function(function);

	const std::size_t         argument_count = callee->arg_size();
	std::vector<llvm::Value*> args(callee->arg_size());

	{
		long current_parameter_offset = m_stack_pointer;
		for (std::size_t i = 0; i < argument_count; ++i)
		{
			int type_id = 0;
			if (function.GetParam(i, &type_id) < 0)
			{
				throw std::runtime_error{
					"something went wrong during registering of system function: nth parameter does not appear to "
					"exist in the script function"};
			}

			llvm::Argument* llvm_argument = &*(callee->arg_begin() + i);
			args[i]                       = load_stack_value(current_parameter_offset, llvm_argument->getType());

			current_parameter_offset -= m_compiler.builder().get_script_type_dword_size(type_id);
		}
	}

	llvm::Value* returned = ir.CreateCall(callee->getFunctionType(), callee, args);

	// todo: store returned m_value
}

llvm::Value* FunctionBuilder::load_stack_value(StackVariableIdentifier i, llvm::Type* type)
{
	return m_compiler.builder().ir().CreateLoad(type, get_stack_value_pointer(i, type));
}

void FunctionBuilder::store_stack_value(StackVariableIdentifier i, llvm::Value* value)
{
	m_compiler.builder().ir().CreateStore(value, get_stack_value_pointer(i, value->getType()));
}

llvm::Value* FunctionBuilder::get_stack_value_pointer(FunctionBuilder::StackVariableIdentifier i, llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	llvm::Value* pointer = get_stack_value_pointer(i);
	return ir.CreateBitCast(pointer, type->getPointerTo());
}

llvm::Value* FunctionBuilder::get_stack_value_pointer(FunctionBuilder::StackVariableIdentifier i)
{
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
	llvm::LLVMContext& context = m_compiler.builder().context();

	// Get a pointer to that argument
	if (i < m_locals_offset)
	{
		std::array<llvm::Value*, 1> indices{{llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), -i)}};

		return ir.CreateGEP(&*(m_llvm_function->arg_begin() + 0), indices);
	}

	// Get a pointer to that value on the local stack
	{
		const std::size_t local_offset = i - m_locals_offset;

		std::array<llvm::Value*, 2> indices{
			{llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), 0),
			 llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), local_offset)}};

		return ir.CreateGEP(m_locals, indices);
	}
}

void FunctionBuilder::store_return_register_value(llvm::Value* value)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	ir.CreateStore(value, get_return_register_pointer(value->getType()));
}

llvm::Value* FunctionBuilder::load_return_register_value(llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	return ir.CreateLoad(type, get_return_register_pointer(type));
}

llvm::Value* FunctionBuilder::get_return_register_pointer(llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	return ir.CreateBitCast(m_value, type->getPointerTo());
}

void FunctionBuilder::insert_label(long offset)
{
	llvm::LLVMContext& context = m_compiler.builder().context();

	auto       emplace_result = m_jump_map.emplace(offset, nullptr);
	const bool success        = emplace_result.second;

	if (!success)
	{
		// there was a label here already; no need to recreate one
		return;
	}

	auto it    = emplace_result.first;
	it->second = llvm::BasicBlock::Create(context, fmt::format("branch_to_{:04x}", offset), m_llvm_function);
}

void FunctionBuilder::preprocess_conditional_branch(FunctionBuilder::InstructionContext instruction)
{
	insert_label(instruction.offset + 2);
	preprocess_unconditional_branch(instruction);
}

void FunctionBuilder::preprocess_unconditional_branch(FunctionBuilder::InstructionContext instruction)
{
	insert_label(instruction.offset + 2 + asBC_INTARG(instruction.pointer));
}

llvm::BasicBlock* FunctionBuilder::get_branch_target(FunctionBuilder::InstructionContext instruction)
{
	auto it = m_jump_map.find(instruction.offset + 2 + asBC_INTARG(instruction.pointer));
	if (it == m_jump_map.end())
	{
		throw std::runtime_error{"something went wrong when getting branch target: mismatch with preprocess"};
	}

	return it->second;
}

llvm::BasicBlock* FunctionBuilder::get_conditional_fail_branch_target(FunctionBuilder::InstructionContext instruction)
{
	return m_jump_map.find(instruction.offset + 2)->second;
}

void FunctionBuilder::emit_branch_if_missing(llvm::BasicBlock* block)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	if (auto* current_terminator = ir.GetInsertBlock()->getTerminator(); current_terminator == nullptr)
	{
		// The current block has no terminator: insert a branch to the current one, which immediately follows it.
		ir.CreateBr(block);
	}

	ir.SetInsertPoint(block);
}
} // namespace asllvm::detail
