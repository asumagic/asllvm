#include <asllvm/detail/functionbuilder.hpp>

#include <array>
#include <asllvm/detail/ashelper.hpp>
#include <asllvm/detail/asinternalheaders.hpp>
#include <asllvm/detail/assert.hpp>
#include <asllvm/detail/debuginfo.hpp>
#include <asllvm/detail/jitcompiler.hpp>
#include <asllvm/detail/llvmglobals.hpp>
#include <asllvm/detail/modulebuilder.hpp>
#include <asllvm/detail/modulecommon.hpp>
#include <fmt/core.h>

namespace asllvm::detail
{
FunctionBuilder::FunctionBuilder(FunctionContext context) : m_context{context}, m_stack{m_context} {}

llvm::Function* FunctionBuilder::translate_bytecode(asDWORD* bytecode, asUINT length)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	llvm::orc::ThreadSafeContext& thread_safe_context = builder.llvm_context();
	auto                          context_lock        = thread_safe_context.getLock();
	auto&                         context             = *thread_safe_context.getContext();

	ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", m_context.llvm_function));

	const auto walk_bytecode = [&](auto&& func) {
		asDWORD* bytecode_current = bytecode;
		asDWORD* bytecode_end     = bytecode + length;

		while (bytecode_current < bytecode_end)
		{
			const asSBCInfo&  info             = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode_current)];
			const std::size_t instruction_size = asBCTypeSize[info.type];

			BytecodeInstruction context{};
			context.pointer = bytecode_current;
			context.info    = &info;
			context.offset  = std::distance(bytecode, bytecode_current);

			func(context);

			bytecode_current += instruction_size;
		}
	};

	try
	{
		if (m_context.compiler->config().verbose)
		{
			fmt::print(stderr, "Function {}\n", m_context.script_function->GetDeclaration(true, true, true));
			fmt::print(
				stderr,
				"scriptData.variableSpace: {}\n"
				"scriptData.stackNeeded: {}\n",
				m_context.script_function->scriptData->variableSpace,
				m_context.script_function->scriptData->stackNeeded);

			fmt::print(stderr, "Disassembly:\n");
			walk_bytecode([this](BytecodeInstruction instruction) {
				const std::string op = disassemble(instruction);
				if (!op.empty())
				{
					fmt::print(stderr, "{:04x}: {}\n", instruction.offset, op);
				}
			});
			fmt::print(stderr, "\n");
		}

		{
			// TODO: this is a bit dumb
			PreprocessContext context;
			walk_bytecode([&](BytecodeInstruction instruction) { preprocess_instruction(instruction, context); });
		}

		create_function_debug_info(m_context.llvm_function, GeneratedFunctionType::Implementation);
		emit_allocate_local_structures();

		walk_bytecode([&](BytecodeInstruction instruction) {
			translate_instruction(instruction);

			// Emit metadata on last inserted instruction for debugging
			if (m_context.compiler->config().verbose)
			{
				llvm::BasicBlock* block = ir.GetInsertBlock();
				if (block->empty())
				{
					return;
				}

				const std::string disassembled = disassemble(instruction);
				if (disassembled.empty())
				{
					return;
				}

				auto& inst = block->back();
				auto* meta = llvm::MDNode::get(context, llvm::MDString::get(context, disassembled));
				inst.setMetadata("asopcode", meta);
			}
		});

		m_stack.finalize();
	}
	catch (std::exception& exception)
	{
		m_context.llvm_function->removeFromParent();
		throw;
	}

	return m_context.llvm_function;
}

llvm::Function* FunctionBuilder::create_vm_entry_thunk()
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	llvm::orc::ThreadSafeContext& thread_safe_context = builder.llvm_context();
	auto                          context_lock        = thread_safe_context.getLock();
	auto&                         context             = *thread_safe_context.getContext();

	const std::array<llvm::Type*, 2> parameter_types{types.vm_registers->getPointerTo(), types.i64};

	llvm::Type* return_type = types.tvoid;

	llvm::Function* wrapper_function = llvm::Function::Create(
		llvm::FunctionType::get(return_type, parameter_types, false),
		llvm::Function::ExternalLinkage,
		make_vm_entry_thunk_name(*m_context.script_function),
		m_context.module_builder->module());

	create_function_debug_info(wrapper_function, GeneratedFunctionType::VmEntryThunk);

	llvm::BasicBlock* block = llvm::BasicBlock::Create(context, "entry", wrapper_function);

	ir.SetInsertPoint(block);

	llvm::Argument* registers = &*(wrapper_function->arg_begin() + 0);
	registers->setName("vmregs");

	llvm::Argument* arg = &*(wrapper_function->arg_begin() + 1);
	arg->setName("jitarg");

	llvm::Value* frame_pointer = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(types.i64, 0), llvm::ConstantInt::get(types.i32, 1)};

		auto* pointer = ir.CreateInBoundsGEP(registers, indices, "stackFramePointerPointer");
		return ir.CreateLoad(types.pi32, pointer, "stackFramePointer");
	}();

	llvm::Value* value_register = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(types.i64, 0), llvm::ConstantInt::get(types.i32, 3)};
		return ir.CreateInBoundsGEP(registers, indices, "valueRegister");
	}();

	llvm::Value* object_register = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(types.i64, 0), llvm::ConstantInt::get(types.i32, 4)};
		return ir.CreateInBoundsGEP(registers, indices, "objectRegister");
	}();

	{
		VmEntryCallContext ctx;
		ctx.vm_frame_pointer = frame_pointer;
		ctx.value_register   = value_register;
		ctx.object_register  = object_register;

		emit_script_call(*m_context.script_function, ctx);
	}

	llvm::Value* program_pointer = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(types.i64, 0), llvm::ConstantInt::get(types.i32, 0)};

		return ir.CreateInBoundsGEP(registers, indices, "programPointer");
	}();

	// Set the program pointer to the RET instruction
	auto* ret_ptr_value = ir.CreateIntToPtr(
		llvm::ConstantInt::get(types.i64, reinterpret_cast<std::uintptr_t>(m_ret_pointer)), types.pi32);
	ir.CreateStore(ret_ptr_value, program_pointer);

	ir.CreateRetVoid();

	return wrapper_function;
}

void FunctionBuilder::preprocess_instruction(BytecodeInstruction instruction, PreprocessContext& ctx)
{
	switch (instruction.info->bc)
	{
	case asBC_JMPP:
	{
		ctx.current_switch_offset = instruction.offset;
		ctx.handling_jump_table   = true;
		break;
	}

	case asBC_JMP:
	{
		preprocess_unconditional_branch(instruction);

		if (ctx.handling_jump_table)
		{
			insert_label(instruction.offset);
			auto [it, success]      = m_switch_map.emplace(ctx.current_switch_offset, std::vector<llvm::BasicBlock*>());
			auto& [offset, targets] = *it;
			targets.push_back(m_jump_map.at(instruction.offset)); // TODO: map lookup is redundant here
		}

		break;
	}

	case asBC_JZ:
	case asBC_JNZ:
	case asBC_JS:
	case asBC_JNS:
	case asBC_JP:
	case asBC_JNP:
	case asBC_JLowZ:
	case asBC_JLowNZ:
	{
		ctx.handling_jump_table = false;
		preprocess_conditional_branch(instruction);
		break;
	}

	default:
	{
		ctx.handling_jump_table = false;
		break;
	}
	}
}

void FunctionBuilder::translate_instruction(BytecodeInstruction ins)
{
	asCScriptEngine&   engine  = m_context.compiler->engine();
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();
	StandardFunctions& funcs   = m_context.module_builder->standard_functions();

	ir.SetCurrentDebugLocation(get_debug_location(m_context, ins.offset, m_context.llvm_function->getSubprogram()));

	const auto old_stack_pointer = m_stack.current_stack_pointer();

	if (auto it = m_jump_map.find(ins.offset); it != m_jump_map.end())
	{
		asllvm_assert(m_stack.empty_stack());
		switch_to_block(it->second);
	}

	m_stack.check_stack_pointer_bounds();

	const auto unimpl = [] { asllvm_assert(false && "unimplemented instruction while translating bytecode"); };

	// TODO: handle division by zero by setting an exception on the context for ALL div AND rem ops

	switch (ins.info->bc)
	{
	case asBC_PopPtr:
	{
		m_stack.pop(AS_PTR_SIZE);
		break;
	}

	case asBC_PshGPtr:
	{
		m_stack.push(load_global(ins.arg_pword(), types.iptr), AS_PTR_SIZE);
		break;
	}

	case asBC_PshC4:
	{
		m_stack.push(llvm::ConstantInt::get(types.i32, ins.arg_dword()), 1);
		break;
	}

	case asBC_PshV4:
	{
		m_stack.push(m_stack.load(ins.arg_sword0(), types.i32), 1);
		break;
	}

	case asBC_PSF:
	{
		m_stack.push(m_stack.pointer_to(ins.arg_sword0(), types.iptr), AS_PTR_SIZE);
		break;
	}

	case asBC_SwapPtr: unimpl(); break;

	case asBC_NOT:
	{
		llvm::Value* source = m_stack.load(ins.arg_sword0(), types.i1);
		llvm::Value* result
			= ir.CreateSelect(source, llvm::ConstantInt::get(types.i32, 0), llvm::ConstantInt::get(types.i32, 1));
		m_stack.store(ins.arg_sword0(), result);
		break;
	}

	case asBC_PshG4:
	{
		m_stack.push(load_global(ins.arg_pword(), types.i32), 1);
		break;
	}

	case asBC_LdGRdR4: unimpl(); break;

	case asBC_CALL:
	{
		auto& function = static_cast<asCScriptFunction&>(*engine.GetFunctionById(ins.arg_int()));
		emit_script_call(function);
		break;
	}

	case asBC_RET:
	{
		const asCDataType& type = m_context.script_function->returnType;

		if (m_context.llvm_function->getReturnType() == types.tvoid)
		{
			ir.CreateRetVoid();
		}
		else if (type.IsObjectHandle() || type.IsObject())
		{
			ir.CreateRet(ir.CreatePointerCast(
				ir.CreateLoad(types.pvoid, m_object_register), m_context.llvm_function->getReturnType()));
		}
		else
		{
			ir.CreateRet(load_value_register_value(m_context.llvm_function->getReturnType()));
		}

		m_ret_pointer = ins.pointer;
		break;
	}

	case asBC_JMP:
	{
		ir.CreateBr(get_branch_target(ins));
		break;
	}

	case asBC_JZ: emit_conditional_branch(ins, llvm::CmpInst::ICMP_EQ); break;
	case asBC_JNZ: emit_conditional_branch(ins, llvm::CmpInst::ICMP_NE); break;
	case asBC_JS: emit_conditional_branch(ins, llvm::CmpInst::ICMP_SLT); break;
	case asBC_JNS: emit_conditional_branch(ins, llvm::CmpInst::ICMP_SGE); break;
	case asBC_JP: emit_conditional_branch(ins, llvm::CmpInst::ICMP_SGT); break;
	case asBC_JNP: emit_conditional_branch(ins, llvm::CmpInst::ICMP_SLE); break;

	case asBC_TZ: emit_condition(llvm::CmpInst::ICMP_EQ); break;
	case asBC_TNZ: emit_condition(llvm::CmpInst::ICMP_NE); break;
	case asBC_TS: emit_condition(llvm::CmpInst::ICMP_SLT); break;
	case asBC_TNS: emit_condition(llvm::CmpInst::ICMP_SGE); break;
	case asBC_TP: emit_condition(llvm::CmpInst::ICMP_SGT); break;
	case asBC_TNP: emit_condition(llvm::CmpInst::ICMP_SLE); break;
	case asBC_NEGi: emit_neg(ins, types.i32); break;
	case asBC_NEGf: emit_neg(ins, types.f32); break;
	case asBC_NEGd: emit_neg(ins, types.f64); break;
	case asBC_INCi16: emit_increment(types.i16, 1); break;
	case asBC_INCi8: emit_increment(types.i8, 1); break;
	case asBC_DECi16: emit_increment(types.i16, -1); break;
	case asBC_DECi8: emit_increment(types.i8, -1); break;
	case asBC_INCi: emit_increment(types.i32, 1); break;
	case asBC_DECi: emit_increment(types.i32, -1); break;
	case asBC_INCf: emit_increment(types.f32, 1); break;
	case asBC_DECf: emit_increment(types.f32, -1); break;
	case asBC_INCd: emit_increment(types.f64, 1); break;
	case asBC_DECd: emit_increment(types.f64, -1); break;

	case asBC_IncVi:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), types.i32);
		llvm::Value* result = ir.CreateAdd(value, llvm::ConstantInt::get(types.i32, 1));
		m_stack.store(ins.arg_sword0(), result);
		break;
	}

	case asBC_DecVi:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), types.i32);
		llvm::Value* result = ir.CreateSub(value, llvm::ConstantInt::get(types.i32, 1));
		m_stack.store(ins.arg_sword0(), result);
		break;
	}

	case asBC_BNOT: emit_bit_not(ins, types.i32); break;
	case asBC_BAND: emit_binop(ins, llvm::Instruction::And, types.i32); break;
	case asBC_BOR: emit_binop(ins, llvm::Instruction::Or, types.i32); break;
	case asBC_BXOR: emit_binop(ins, llvm::Instruction::Xor, types.i32); break;
	case asBC_BSLL: emit_binop(ins, llvm::Instruction::Shl, types.i32); break;
	case asBC_BSRL: emit_binop(ins, llvm::Instruction::LShr, types.i32); break;
	case asBC_BSRA: emit_binop(ins, llvm::Instruction::AShr, types.i32); break;

	case asBC_COPY: unimpl(); break;

	case asBC_PshC8:
	{
		m_stack.push(llvm::ConstantInt::get(types.i64, ins.arg_qword()), 2);
		break;
	}

	case asBC_PshVPtr:
	{
		m_stack.push(m_stack.load(ins.arg_sword0(), types.iptr), AS_PTR_SIZE);
		break;
	}

	case asBC_RDSPtr:
	{
		// Dereference pointer from the top of stack, set the top of the stack to the dereferenced value.

		// FIXME: check pointer and raise VM exception TXT_NULL_POINTER_ACCESS when null
		llvm::Value* address = m_stack.top(types.pvoid->getPointerTo());
		llvm::Value* value   = ir.CreateLoad(types.pvoid, address);
		m_stack.store(m_stack.current_stack_pointer(), value);
		break;
	}

	case asBC_CMPd:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.f64);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), types.f64);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPu:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.i32);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), types.i32);
		emit_compare(lhs, rhs, false);
		break;
	}

	case asBC_CMPf:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.f32);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), types.f32);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPi:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.i32);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), types.i32);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPIi:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.i32);
		llvm::Value* rhs = llvm::ConstantInt::get(types.i32, ins.arg_int());
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPIf:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.f32);
		llvm::Value* rhs = llvm::ConstantInt::get(types.f32, ins.arg_float());
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPIu:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.i32);
		llvm::Value* rhs = llvm::ConstantInt::get(types.i32, ins.arg_int());
		emit_compare(lhs, rhs, false);
		break;
	}

	case asBC_JMPP:
	{
		auto& targets = m_switch_map.at(ins.offset);
		asllvm_assert(!targets.empty());

		llvm::SwitchInst* inst
			= ir.CreateSwitch(m_stack.load(ins.arg_sword0(), types.i32), targets.back(), targets.size());

		for (std::size_t i = 0; i < targets.size(); ++i)
		{
			inst->addCase(llvm::ConstantInt::get(types.i32, i), targets[i]);
		}

		break;
	}

	case asBC_PopRPtr:
	{
		store_value_register_value(m_stack.pop(AS_PTR_SIZE, types.iptr));
		break;
	}

	case asBC_PshRPtr:
	{
		m_stack.push(load_value_register_value(types.iptr), AS_PTR_SIZE);
		break;
	}

	case asBC_STR: asllvm_assert(false && "STR is deperecated and should not have been emitted by AS"); break;

	case asBC_CALLSYS:
	case asBC_Thiscall1:
	{
		asCScriptFunction* function = static_cast<asCScriptFunction*>(engine.GetFunctionById(ins.arg_int()));
		asllvm_assert(function != nullptr);
		emit_system_call(*function);

		break;
	}

	case asBC_CALLBND: unimpl(); break;

	case asBC_SUSPEND:
	{
		if (m_context.compiler->config().verbose)
		{
			m_context.compiler->diagnostic("Found VM suspend, these are unsupported and ignored", asMSGTYPE_WARNING);
		}

		break;
	}

	case asBC_ALLOC:
	{
		auto&     type           = *reinterpret_cast<asCObjectType*>(ins.arg_pword());
		const int constructor_id = ins.arg_int(AS_PTR_SIZE);

		if (type.flags & asOBJ_SCRIPT_OBJECT)
		{
			// Initialize stuff using the scriptobject constructor
			std::array<llvm::Value*, 1> args{
				{ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, reinterpret_cast<asPWORD>(&type)), types.pvoid)}};

			llvm::Value* object_memory_pointer
				= ir.CreateCall(funcs.new_script_object, args, fmt::format("dynamic.{}", type.GetName()));

			// Constructor
			asCScriptFunction& constructor = *static_cast<asCScriptEngine&>(engine).scriptFunctions[constructor_id];

			llvm::Value* target_pointer = m_stack.load(
				m_stack.current_stack_pointer() - constructor.GetSpaceNeededForArguments(),
				types.pvoid->getPointerTo());

			// TODO: check if target_pointer is null before the store (we really should)
			ir.CreateStore(object_memory_pointer, target_pointer);

			m_stack.push(object_memory_pointer, AS_PTR_SIZE);
			emit_script_call(constructor);

			m_stack.pop(AS_PTR_SIZE);
		}
		else
		{
			// Allocate memory for the object
			std::array<llvm::Value*, 1> alloc_args{
				llvm::ConstantInt::get(types.iptr, type.size) // TODO: align type.size to 4 bytes
			};

			llvm::Value* object_memory_pointer
				= ir.CreateCall(funcs.alloc, alloc_args, fmt::format("heap.{}", type.GetName()));

			if (constructor_id != 0)
			{
				m_stack.push(object_memory_pointer, AS_PTR_SIZE);
				emit_system_call(*static_cast<asCScriptEngine&>(engine).scriptFunctions[constructor_id]);
			}

			llvm::Value* target_address = m_stack.pop(AS_PTR_SIZE, types.pvoid->getPointerTo());

			// FIXME: check for null pointer (check what VM does in that context)
			ir.CreateStore(object_memory_pointer, target_address);

			// TODO: check for suspend?
		}

		break;
	}

	case asBC_FREE:
	{
		asCObjectType&    object_type = *reinterpret_cast<asCObjectType*>(ins.arg_pword());
		asSTypeBehaviour& beh         = object_type.beh;

		// FIXME: check pointer and _ignore if null_

		llvm::Value* variable_pointer = m_stack.pointer_to(ins.arg_sword0(), types.pvoid);
		llvm::Value* object_pointer   = ir.CreateLoad(types.pvoid, variable_pointer);

		if ((object_type.flags & asOBJ_REF) != 0)
		{
			asllvm_assert((object_type.flags & asOBJ_NOCOUNT) != 0 || beh.release != 0);

			if (beh.release != 0)
			{
				emit_object_method_call(*engine.scriptFunctions[beh.release], object_pointer);
			}
		}
		else
		{
			if (beh.destruct != 0)
			{
				emit_object_method_call(
					*static_cast<asCScriptEngine&>(engine).scriptFunctions[beh.destruct], object_pointer);
			}
			else if ((object_type.flags & asOBJ_LIST_PATTERN) != 0)
			{
				m_context.compiler->diagnostic(
					"STUB: asOBJ_LIST_PATTERN free. this will result in a leak.", asMSGTYPE_WARNING);
			}

			{
				std::array<llvm::Value*, 1> args{object_pointer};
				ir.CreateCall(funcs.free, args);
			}
		}

		break;
	}

	case asBC_LOADOBJ:
	{
		llvm::Value* pointer_to_object = m_stack.load(ins.arg_sword0(), types.pvoid);
		ir.CreateStore(pointer_to_object, m_object_register);
		m_stack.store(ins.arg_sword0(), ir.CreatePtrToInt(llvm::ConstantInt::get(types.iptr, 0), types.pvoid));

		break;
	}

	case asBC_STOREOBJ:
	{
		m_stack.store(ins.arg_sword0(), ir.CreateLoad(types.pvoid, m_object_register));
		ir.CreateStore(ir.CreatePtrToInt(llvm::ConstantInt::get(types.iptr, 0), types.pvoid), m_object_register);
		break;
	}

	case asBC_GETOBJ:
	{
		// Replace a variable index by a pointer to the value

		llvm::Value* offset_pointer = m_stack.pointer_to(m_stack.current_stack_pointer() - ins.arg_word0(), types.iptr);
		llvm::Value* offset         = ir.CreateLoad(types.iptr, offset_pointer);

		// Get pointer to where the pointer value on the stack is
		std::array<llvm::Value*, 2> gep_offset{
			llvm::ConstantInt::get(types.iptr, 0),
			ir.CreateSub(llvm::ConstantInt::get(types.iptr, m_stack.total_space()), offset, "addr", true, true)};

		llvm::Value* variable_pointer
			= ir.CreatePointerCast(ir.CreateInBoundsGEP(m_stack.storage_alloca(), gep_offset), types.piptr);
		llvm::Value* variable = ir.CreateLoad(types.iptr, variable_pointer);

		ir.CreateStore(variable, offset_pointer);
		ir.CreateStore(llvm::ConstantInt::get(types.iptr, 0), variable_pointer);
		break;
	}

	case asBC_REFCPY:
	{
		asCObjectType&    object_type = *reinterpret_cast<asCObjectType*>(ins.arg_pword());
		asSTypeBehaviour& beh         = object_type.beh;

		llvm::Value* destination = m_stack.pop(AS_PTR_SIZE, types.pvoid->getPointerTo());
		llvm::Value* reference   = m_stack.top(types.pvoid);

		if ((object_type.flags & asOBJ_NOCOUNT) == 0)
		{
			if (beh.release != 0)
			{
				m_context.compiler->diagnostic(
					"STUB: asBC_REFCPY release of old pointer. this may result in a leak.", asMSGTYPE_WARNING);
			}

			// FIXME: check reference pointer and do nothing if null.

			if (beh.addref != 0)
			{
				m_context.compiler->diagnostic("STUB: not checking for zero in addref");

				std::array<llvm::Value*, 2> args{
					reference,
					ir.CreateIntToPtr(
						llvm::ConstantInt::get(
							types.iptr, reinterpret_cast<asPWORD>(engine.GetScriptFunction(object_type.beh.addref))),
						types.pvoid)};

				ir.CreateCall(funcs.call_object_method, args);
			}
		}

		ir.CreateStore(reference, destination);

		break;
	}

	case asBC_CHKREF:
	{
		// FIXME: check pointer and raise VM exception TXT_NULL_POINTER_ACCESS when null
		break;
	}

	// Replace a variable index on the stack with the object handle stored in that variable.
	case asBC_GETOBJREF:
	{
		llvm::Value* pointer = m_stack.pointer_to(m_stack.current_stack_pointer() - ins.arg_word0(), types.iptr);

		llvm::Value*                      index = ir.CreateLoad(types.iptr, pointer);
		const std::array<llvm::Value*, 2> indices{
			llvm::ConstantInt::get(types.iptr, 0),
			ir.CreateSub(llvm::ConstantInt::get(types.iptr, m_stack.total_space()), index)};

		llvm::Value* variable_address = ir.CreatePointerCast(
			ir.CreateInBoundsGEP(m_stack.storage_alloca(), indices), types.iptr->getPointerTo()->getPointerTo());

		llvm::Value* variable = ir.CreateLoad(types.iptr->getPointerTo(), variable_address);

		ir.CreateStore(variable, ir.CreatePointerCast(pointer, types.iptr->getPointerTo()->getPointerTo()));

		break;
	}

	// Replace a variable index on the stack with the address of the variable.
	case asBC_GETREF:
	{
		llvm::Value* pointer = m_stack.pointer_to(m_stack.current_stack_pointer() - ins.arg_word0(), types.pi32);

		llvm::Value*                      index = ir.CreateLoad(types.i32, ir.CreatePointerCast(pointer, types.pi32));
		const std::array<llvm::Value*, 2> indices{
			llvm::ConstantInt::get(types.i64, 0),
			ir.CreateSub(llvm::ConstantInt::get(types.i32, m_stack.total_space()), index)};
		llvm::Value* variable_address = ir.CreateInBoundsGEP(m_stack.storage_alloca(), indices);

		ir.CreateStore(variable_address, ir.CreatePointerCast(pointer, types.pi32->getPointerTo()));

		break;
	}

	case asBC_PshNull: unimpl(); break;
	case asBC_ClrVPtr: unimpl(); break;

	case asBC_OBJTYPE:
	{
		m_stack.push(llvm::ConstantInt::get(types.iptr, ins.arg_pword()), AS_PTR_SIZE);
		break;
	}

	case asBC_TYPEID: unimpl(); break;

	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:
	{
		m_stack.store(ins.arg_sword0(), llvm::ConstantInt::get(types.i32, ins.arg_dword()));
		break;
	}

	case asBC_SetV8:
	{
		m_stack.store(ins.arg_sword0(), llvm::ConstantInt::get(types.i64, ins.arg_qword()));
		break;
	}

	case asBC_ADDSi:
	{
		llvm::Value* stack_pointer = m_stack.pointer_to(m_stack.current_stack_pointer(), types.iptr);

		// TODO: Check for null pointer
		llvm::Value* original_value = ir.CreateLoad(types.iptr, stack_pointer);
		llvm::Value* incremented_value
			= ir.CreateAdd(original_value, llvm::ConstantInt::get(types.iptr, ins.arg_sword0()));

		ir.CreateStore(incremented_value, stack_pointer);
		break;
	}

	case asBC_CpyVtoV4:
	{
		auto target = ins.arg_sword0();
		auto source = ins.arg_sword1();

		m_stack.store(target, m_stack.load(source, types.i32));

		break;
	}

	case asBC_CpyVtoV8:
	{
		auto target = ins.arg_sword0();
		auto source = ins.arg_sword1();

		m_stack.store(target, m_stack.load(source, types.i64));

		break;
	}

	case asBC_CpyVtoR4:
	{
		store_value_register_value(m_stack.load(ins.arg_sword0(), types.i32));
		break;
	}

	case asBC_CpyVtoR8:
	{
		store_value_register_value(m_stack.load(ins.arg_sword0(), types.i64));
		break;
	}

	case asBC_CpyVtoG4:
	{
		llvm::Value* global_ptr = ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, ins.arg_pword()), types.pi32);
		llvm::Value* value      = m_stack.load(ins.arg_sword0(), types.i32);
		ir.CreateStore(value, global_ptr);
		break;
	}

	case asBC_CpyRtoV4:
	{
		m_stack.store(ins.arg_sword0(), load_value_register_value(types.i32));
		break;
	}

	case asBC_CpyRtoV8:
	{
		m_stack.store(ins.arg_sword0(), load_value_register_value(types.i64));
		break;
	}

	case asBC_CpyGtoV4:
	{
		llvm::Value* global_ptr = ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, ins.arg_pword()), types.pi32);
		m_stack.store(ins.arg_sword0(), ir.CreateLoad(global_ptr, types.i32));
		break;
	}

	case asBC_WRTV1:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), types.i8);
		llvm::Value* target = load_value_register_value(types.pi8);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_WRTV2:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), types.i16);
		llvm::Value* target = load_value_register_value(types.pi16);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_WRTV4:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), types.i32);
		llvm::Value* target = load_value_register_value(types.pi32);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_WRTV8:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), types.i64);
		llvm::Value* target = load_value_register_value(types.pi64);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_RDR1:
	{
		llvm::Value* source_pointer = load_value_register_value(types.pi8);
		llvm::Value* source_word    = ir.CreateLoad(types.i8, source_pointer);
		llvm::Value* source         = ir.CreateZExt(source_word, types.i32);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_RDR2:
	{
		llvm::Value* source_pointer = load_value_register_value(types.pi16);
		llvm::Value* source_word    = ir.CreateLoad(types.i16, source_pointer);
		llvm::Value* source         = ir.CreateZExt(source_word, types.i32);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_RDR4:
	{
		llvm::Value* source_pointer = load_value_register_value(types.pi32);
		llvm::Value* source         = ir.CreateLoad(types.i32, source_pointer);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_RDR8:
	{
		llvm::Value* source_pointer = load_value_register_value(types.pi64);
		llvm::Value* source         = ir.CreateLoad(types.i64, source_pointer);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_LDG:
	{
		store_value_register_value(ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, ins.arg_pword()), types.pvoid));
		break;
	}

	case asBC_LDV:
	{
		store_value_register_value(m_stack.pointer_to(ins.arg_sword0(), types.pvoid));
		break;
	}

	case asBC_PGA:
	{
		m_stack.push(llvm::ConstantInt::get(types.i64, ins.arg_pword()), AS_PTR_SIZE);
		break;
	}

	case asBC_CmpPtr: unimpl(); break;

	case asBC_VAR:
	{
		m_stack.push(llvm::ConstantInt::get(types.i64, ins.arg_sword0()), AS_PTR_SIZE);
		break;
	}

	case asBC_iTOf: emit_cast(ins, llvm::Instruction::SIToFP, types.i32, types.f32); break;
	case asBC_fTOi: emit_cast(ins, llvm::Instruction::FPToSI, types.f32, types.i32); break;
	case asBC_uTOf: emit_cast(ins, llvm::Instruction::UIToFP, types.i32, types.f32); break;
	case asBC_fTOu: emit_cast(ins, llvm::Instruction::FPToUI, types.f32, types.i32); break;

	case asBC_sbTOi: emit_cast(ins, llvm::Instruction::SExt, types.i8, types.i32); break;
	case asBC_swTOi: emit_cast(ins, llvm::Instruction::SExt, types.i16, types.i32); break;
	case asBC_ubTOi: emit_cast(ins, llvm::Instruction::ZExt, types.i8, types.i32); break;
	case asBC_uwTOi: emit_cast(ins, llvm::Instruction::ZExt, types.i16, types.i32); break;

	case asBC_dTOi: emit_cast(ins, llvm::Instruction::FPToSI, types.f64, types.i32); break;
	case asBC_dTOu: emit_cast(ins, llvm::Instruction::FPToUI, types.f64, types.i32); break;
	case asBC_dTOf: emit_cast(ins, llvm::Instruction::FPTrunc, types.f64, types.f32); break;

	case asBC_iTOd: emit_cast(ins, llvm::Instruction::SIToFP, types.i32, types.f64); break;
	case asBC_uTOd: emit_cast(ins, llvm::Instruction::UIToFP, types.i32, types.f64); break;
	case asBC_fTOd: emit_cast(ins, llvm::Instruction::FPExt, types.f32, types.f64); break;

	case asBC_ADDi: emit_binop(ins, llvm::Instruction::Add, types.i32); break;
	case asBC_SUBi: emit_binop(ins, llvm::Instruction::Sub, types.i32); break;
	case asBC_MULi: emit_binop(ins, llvm::Instruction::Mul, types.i32); break;
	case asBC_DIVi: emit_binop(ins, llvm::Instruction::SDiv, types.i32); break;
	case asBC_MODi: emit_binop(ins, llvm::Instruction::SRem, types.i32); break;

	case asBC_ADDf: emit_binop(ins, llvm::Instruction::FAdd, types.f32); break;
	case asBC_SUBf: emit_binop(ins, llvm::Instruction::FSub, types.f32); break;
	case asBC_MULf: emit_binop(ins, llvm::Instruction::FMul, types.f32); break;
	case asBC_DIVf: emit_binop(ins, llvm::Instruction::FDiv, types.f32); break;
	case asBC_MODf: emit_binop(ins, llvm::Instruction::FRem, types.f32); break;

	case asBC_ADDd: emit_binop(ins, llvm::Instruction::FAdd, types.f64); break;
	case asBC_SUBd: emit_binop(ins, llvm::Instruction::FSub, types.f64); break;
	case asBC_MULd: emit_binop(ins, llvm::Instruction::FMul, types.f64); break;
	case asBC_DIVd: emit_binop(ins, llvm::Instruction::FDiv, types.f64); break;
	case asBC_MODd: emit_binop(ins, llvm::Instruction::FRem, types.f64); break;

	case asBC_ADDIi: emit_binop(ins, llvm::Instruction::Add, llvm::ConstantInt::get(types.i32, ins.arg_int(1))); break;
	case asBC_SUBIi: emit_binop(ins, llvm::Instruction::Sub, llvm::ConstantInt::get(types.i32, ins.arg_int(1))); break;
	case asBC_MULIi: emit_binop(ins, llvm::Instruction::Mul, llvm::ConstantInt::get(types.i32, ins.arg_int(1))); break;

	case asBC_ADDIf:
		emit_binop(ins, llvm::Instruction::FAdd, llvm::ConstantFP::get(types.f32, ins.arg_float(1)));
		break;
	case asBC_SUBIf:
		emit_binop(ins, llvm::Instruction::FSub, llvm::ConstantFP::get(types.f32, ins.arg_float(1)));
		break;
	case asBC_MULIf:
		emit_binop(ins, llvm::Instruction::FMul, llvm::ConstantFP::get(types.f32, ins.arg_float(1)));
		break;

	case asBC_SetG4:
	{
		llvm::Value* pointer = ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, ins.arg_pword()), types.pi32);
		llvm::Value* value   = llvm::ConstantInt::get(types.i32, ins.arg_dword(AS_PTR_SIZE));
		ir.CreateStore(value, pointer);
		break;
	}

	case asBC_ChkRefS: unimpl(); break;

	case asBC_ChkNullV:
	{
		// FIXME: check pointer and raise VM exception TXT_NULL_POINTER_ACCESS when null
		break;
	}

	case asBC_CALLINTF:
	{
		auto& function = static_cast<asCScriptFunction&>(*engine.GetFunctionById(ins.arg_int()));
		emit_script_call(function);
		break;
	}

	case asBC_iTOb: emit_cast(ins, llvm::Instruction::Trunc, types.i32, types.i8); break;
	case asBC_iTOw: emit_cast(ins, llvm::Instruction::Trunc, types.i32, types.i16); break;

	case asBC_Cast: unimpl(); break;

	case asBC_i64TOi: emit_cast(ins, llvm::Instruction::Trunc, types.i64, types.i32); break;
	case asBC_uTOi64: emit_cast(ins, llvm::Instruction::ZExt, types.i32, types.i64); break;
	case asBC_iTOi64: emit_cast(ins, llvm::Instruction::SExt, types.i32, types.i64); break;

	case asBC_fTOi64: emit_cast(ins, llvm::Instruction::FPToSI, types.f32, types.i64); break;
	case asBC_dTOi64: emit_cast(ins, llvm::Instruction::FPToSI, types.f64, types.i64); break;
	case asBC_fTOu64: emit_cast(ins, llvm::Instruction::FPToUI, types.f32, types.i64); break;
	case asBC_dTOu64: emit_cast(ins, llvm::Instruction::FPToUI, types.f64, types.i64); break;

	case asBC_i64TOf: emit_cast(ins, llvm::Instruction::SIToFP, types.i64, types.f32); break;
	case asBC_u64TOf: emit_cast(ins, llvm::Instruction::UIToFP, types.i64, types.f32); break;
	case asBC_i64TOd: emit_cast(ins, llvm::Instruction::SIToFP, types.i64, types.f64); break;
	case asBC_u64TOd: emit_cast(ins, llvm::Instruction::UIToFP, types.i64, types.f64); break;

	case asBC_NEGi64: emit_neg(ins, types.i64); break;
	case asBC_INCi64: emit_increment(types.i64, 1); break;
	case asBC_DECi64: emit_increment(types.i64, -1); break;
	case asBC_BNOT64: emit_bit_not(ins, types.i64); break;

	case asBC_ADDi64: emit_binop(ins, llvm::Instruction::Add, types.i64); break;
	case asBC_SUBi64: emit_binop(ins, llvm::Instruction::Sub, types.i64); break;
	case asBC_MULi64: emit_binop(ins, llvm::Instruction::Mul, types.i64); break;
	case asBC_DIVi64: emit_binop(ins, llvm::Instruction::SDiv, types.i64); break;
	case asBC_MODi64: emit_binop(ins, llvm::Instruction::SRem, types.i64); break;

	case asBC_BAND64: emit_binop(ins, llvm::Instruction::And, types.i64); break;
	case asBC_BOR64: emit_binop(ins, llvm::Instruction::Or, types.i64); break;
	case asBC_BXOR64: emit_binop(ins, llvm::Instruction::Xor, types.i64); break;
	case asBC_BSLL64: emit_binop(ins, llvm::Instruction::Shl, types.i64); break;
	case asBC_BSRL64: emit_binop(ins, llvm::Instruction::LShr, types.i64); break;
	case asBC_BSRA64: emit_binop(ins, llvm::Instruction::AShr, types.i64); break;

	case asBC_CMPi64:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.i64);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), types.i64);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPu64:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), types.i64);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), types.i64);
		emit_compare(lhs, rhs, false);
		break;
	}

	case asBC_ChkNullS: unimpl(); break;

	case asBC_ClrHi:
	{
		// TODO: untested. What does emit this? Is it possible to see this in final _optimized_ bytecode?
		llvm::Value* source_value = load_value_register_value(types.i8);
		llvm::Value* extended     = ir.CreateZExt(source_value, types.i32);
		store_value_register_value(extended);
		break;
	}

	case asBC_JitEntry:
	{
		if (m_context.compiler->config().verbose)
		{
			m_context.compiler->diagnostic("Found JIT entry point, patching as valid entry point");
		}

		// Pass the JitCompiler as the jitArg value, which can be used by lazy_jit_compiler().
		// TODO: this is probably UB
		ins.arg_pword() = reinterpret_cast<asPWORD>(m_context.compiler);

		break;
	}

	case asBC_CallPtr: unimpl(); break;
	case asBC_FuncPtr: unimpl(); break;

	case asBC_LoadThisR:
	{
		llvm::Value* object = m_stack.load(0, types.pvoid);

		// FIXME: check pointer and raise VM exception TXT_NULL_POINTER_ACCESS when null

		std::array<llvm::Value*, 1> indices{{llvm::ConstantInt::get(types.iptr, ins.arg_sword0())}};
		llvm::Value*                field = ir.CreateInBoundsGEP(object, indices);

		store_value_register_value(field);

		break;
	}

	case asBC_PshV8: m_stack.push(m_stack.load(ins.arg_sword0(), types.i64), 2); break;

	case asBC_DIVu: emit_binop(ins, llvm::Instruction::UDiv, types.i32); break;
	case asBC_MODu: emit_binop(ins, llvm::Instruction::URem, types.i32); break;

	case asBC_DIVu64: emit_binop(ins, llvm::Instruction::UDiv, types.i64); break;
	case asBC_MODu64: emit_binop(ins, llvm::Instruction::URem, types.i64); break;

	case asBC_LoadRObjR:
	{
		llvm::Value* base_pointer = m_stack.load(ins.arg_sword0(), types.pvoid);

		// FIXME: check pointer and raise VM exception TXT_NULL_POINTER_ACCESS when null

		std::array<llvm::Value*, 1> offsets{llvm::ConstantInt::get(types.iptr, ins.arg_sword1())};
		llvm::Value*                pointer = ir.CreateInBoundsGEP(base_pointer, offsets, "fieldptr");

		store_value_register_value(pointer);

		break;
	}

	case asBC_LoadVObjR: unimpl(); break;

	case asBC_RefCpyV:
	{
		auto& object_type = *reinterpret_cast<asCObjectType*>(ins.arg_pword());
		// asSTypeBehaviour& beh         = object_type.beh;

		llvm::Value* destination = m_stack.pointer_to(ins.arg_sword0(), types.pvoid);
		llvm::Value* s           = m_stack.top(types.pvoid);

		if ((object_type.flags & asOBJ_NOCOUNT) == 0)
		{
			if (object_type.beh.release != 0)
			{
				m_context.compiler->diagnostic("STUB: asBC_RefCpyV not calling release on old ref!");
			}

			m_context.compiler->diagnostic("STUB: not checking for zero in addref");

			std::array<llvm::Value*, 2> args{
				s,
				ir.CreateIntToPtr(
					llvm::ConstantInt::get(
						types.iptr, reinterpret_cast<asPWORD>(engine.GetScriptFunction(object_type.beh.addref))),
					types.pvoid)};

			ir.CreateCall(funcs.call_object_method, args);
		}

		ir.CreateStore(s, destination);

		break;
	}

	case asBC_JLowZ:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_EQ, load_value_register_value(types.i8), llvm::ConstantInt::get(types.i8, 0));

		ir.CreateCondBr(condition, get_branch_target(ins), get_conditional_fail_branch_target(ins));

		break;
	}

	case asBC_JLowNZ:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_NE, load_value_register_value(types.i8), llvm::ConstantInt::get(types.i8, 0));

		ir.CreateCondBr(condition, get_branch_target(ins), get_conditional_fail_branch_target(ins));

		break;
	}

	case asBC_AllocMem:
	{
		const auto bytes = ins.arg_dword();

		std::array<llvm::Value*, 1> args{llvm::ConstantInt::get(types.iptr, bytes)};
		m_stack.store(ins.arg_sword0(), ir.CreateCall(funcs.alloc, args, "listMemory"));

		break;
	}

	case asBC_SetListSize:
	{
		llvm::Value* list_pointer       = m_stack.load(ins.arg_sword0(), types.pvoid);
		const auto   offset_within_list = ins.arg_dword();
		const auto   size               = ins.arg_dword(1);

		std::array<llvm::Value*, 1> indices{llvm::ConstantInt::get(types.iptr, offset_within_list)};
		llvm::Value*                target_pointer = ir.CreateInBoundsGEP(list_pointer, indices);
		llvm::Value* typed_target_pointer = ir.CreatePointerCast(target_pointer, types.pi32, "listSizePointer");

		ir.CreateStore(llvm::ConstantInt::get(types.i32, size), typed_target_pointer);
		break;
	}

	case asBC_PshListElmnt:
	{
		llvm::Value* list_pointer       = m_stack.load(ins.arg_sword0(), types.pvoid);
		const auto   offset_within_list = ins.arg_dword();

		std::array<llvm::Value*, 1> indices{llvm::ConstantInt::get(types.iptr, offset_within_list)};
		llvm::Value*                target_pointer = ir.CreateInBoundsGEP(list_pointer, indices);

		m_stack.push(target_pointer, AS_PTR_SIZE);
		break;
	}

	case asBC_SetListType: unimpl(); break;
	case asBC_POWi: unimpl(); break;
	case asBC_POWu: unimpl(); break;
	case asBC_POWf: unimpl(); break;
	case asBC_POWd: unimpl(); break;
	case asBC_POWdi: unimpl(); break;
	case asBC_POWi64: unimpl(); break;
	case asBC_POWu64: unimpl(); break;

	default:
	{
		asllvm_assert(false && "unrecognized instruction - are you using an unsupported AS version?");
	}
	}

	if (const auto expected_increment = ins.info->stackInc; expected_increment != 0xFFFF)
	{
		asllvm_assert(m_stack.current_stack_pointer() - old_stack_pointer == expected_increment);
	}
}

std::string FunctionBuilder::disassemble(BytecodeInstruction instruction)
{
	// Handle certain instructions specifically
	switch (instruction.info->bc)
	{
	case asBC_JitEntry:
	case asBC_SUSPEND:
	{
		return {};
	}

	case asBC_CALL:
	case asBC_CALLSYS:
	case asBC_Thiscall1:
	{
		auto* func
			= static_cast<asCScriptFunction*>(m_context.compiler->engine().GetFunctionById(instruction.arg_int()));

		asllvm_assert(func != nullptr);

		return fmt::format(
			"{} {} # {}", instruction.info->name, func->GetName(), func->GetDeclaration(true, true, true));
	}

	default: break;
	}

	// Disassemble based on the generic instruction type
	// TODO: figure out variable references
	switch (instruction.info->type)
	{
	case asBCTYPE_NO_ARG:
	{
		return fmt::format("{}", instruction.info->name);
	}

	case asBCTYPE_W_ARG:
	case asBCTYPE_wW_ARG:
	case asBCTYPE_rW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, instruction.arg_sword0());
	}

	case asBCTYPE_DW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, instruction.arg_int());
	}

	case asBCTYPE_rW_DW_ARG:
	case asBCTYPE_wW_DW_ARG:
	case asBCTYPE_W_DW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_int());
	}

	case asBCTYPE_QW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, instruction.arg_pword());
	}

	case asBCTYPE_DW_DW_ARG:
	{
		// TODO: double check this
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_int(), instruction.arg_int(1));
	}

	case asBCTYPE_wW_rW_rW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			instruction.arg_sword0(),
			instruction.arg_sword1(),
			instruction.arg_sword2());
	}

	case asBCTYPE_wW_QW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_pword(1));
	}

	case asBCTYPE_wW_rW_ARG:
	case asBCTYPE_rW_rW_ARG:
	case asBCTYPE_wW_W_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_sword1());
	}

	case asBCTYPE_wW_rW_DW_ARG:
	case asBCTYPE_rW_W_DW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			instruction.arg_sword0(),
			instruction.arg_sword1(),
			instruction.arg_int(1));
	}

	case asBCTYPE_QW_DW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_pword(), instruction.arg_int(2));
	}

	case asBCTYPE_rW_QW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_pword(1));
	}

	case asBCTYPE_rW_DW_DW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			instruction.arg_sword0(),
			instruction.arg_int(1),
			instruction.arg_int(2));
	}

	default:
	{
		return fmt::format("(unimplemented)");
	}
	}
}

void FunctionBuilder::emit_allocate_local_structures()
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	m_value_register  = ir.CreateAlloca(types.i64, nullptr, "valueRegister");
	m_object_register = ir.CreateAlloca(types.pvoid, nullptr, "objectRegister");
	m_stack.setup();
}

void FunctionBuilder::emit_cast(
	BytecodeInstruction        instruction,
	llvm::Instruction::CastOps op,
	llvm::Type*                source_type,
	llvm::Type*                destination_type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	const bool source_64      = source_type == types.i64 || source_type == types.f64;
	const bool destination_64 = destination_type == types.i64 || destination_type == types.f64;

	// TODO: more robust check for this
	const auto stack_offset = (source_64 != destination_64) ? instruction.arg_sword1() : instruction.arg_sword0();

	llvm::Value* converted = ir.CreateCast(op, m_stack.load(stack_offset, source_type), destination_type);

	m_stack.store(instruction.arg_sword0(), converted);
}

void FunctionBuilder::emit_binop(BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Type* type)
{
	return emit_binop(instruction, op, m_stack.load(instruction.arg_sword2(), type));
}

void FunctionBuilder::emit_binop(BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Value* rhs)
{
	return emit_binop(instruction, op, m_stack.load(instruction.arg_sword1(), rhs->getType()), rhs);
}

void FunctionBuilder::emit_binop(
	BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Value* lhs, llvm::Value* rhs)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	m_stack.store(instruction.arg_sword0(), ir.CreateBinOp(op, lhs, rhs));
}

void FunctionBuilder::emit_neg(BytecodeInstruction instruction, llvm::Type* type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	const bool is_float = type->isFloatingPointTy();

	llvm::Value* lhs    = is_float ? llvm::ConstantFP::get(type, 0.0) : llvm::ConstantInt::get(type, 0);
	llvm::Value* rhs    = m_stack.load(instruction.arg_sword0(), type);
	llvm::Value* result = ir.CreateBinOp(is_float ? llvm::Instruction::FSub : llvm::Instruction::Sub, lhs, rhs);
	m_stack.store(instruction.arg_sword0(), result);
}

void FunctionBuilder::emit_bit_not(BytecodeInstruction instruction, llvm::Type* type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	llvm::Value* lhs    = llvm::ConstantInt::get(type, -1);
	llvm::Value* rhs    = m_stack.load(instruction.arg_sword0(), type);
	llvm::Value* result = ir.CreateXor(lhs, rhs);
	m_stack.store(instruction.arg_sword0(), result);
}

void FunctionBuilder::emit_condition(llvm::CmpInst::Predicate pred)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	llvm::Value* lhs    = load_value_register_value(types.i32);
	llvm::Value* rhs    = llvm::ConstantInt::get(types.i32, 0);
	llvm::Value* result = ir.CreateICmp(pred, lhs, rhs);
	store_value_register_value(ir.CreateZExt(result, types.i64));
}

void FunctionBuilder::emit_increment(llvm::Type* value_type, long by)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	const bool is_float = value_type->isFloatingPointTy();

	llvm::Value* increment = is_float ? llvm::ConstantFP::get(value_type, by) : llvm::ConstantInt::get(value_type, by);
	llvm::Instruction::BinaryOps op = is_float ? llvm::Instruction::FAdd : llvm::Instruction::Add;

	llvm::Value* value_pointer = load_value_register_value(value_type->getPointerTo());
	llvm::Value* value         = ir.CreateLoad(value_type, value_pointer);
	llvm::Value* incremented   = ir.CreateBinOp(op, value, increment);
	ir.CreateStore(incremented, value_pointer);
}

void FunctionBuilder::emit_compare(llvm::Value* lhs, llvm::Value* rhs, bool is_signed)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	asllvm_assert(lhs->getType() == rhs->getType());

	const bool is_float = lhs->getType()->isFloatingPointTy();

	llvm::Value* constant_lt = llvm::ConstantInt::get(types.i32, -1);
	llvm::Value* constant_eq = llvm::ConstantInt::get(types.i32, 0);
	llvm::Value* constant_gt = llvm::ConstantInt::get(types.i32, 1);

	llvm::Value *lower_than, *greater_than;

	if (is_float)
	{
		lower_than   = ir.CreateFCmp(llvm::CmpInst::FCMP_OLT, lhs, rhs);
		greater_than = ir.CreateFCmp(llvm::CmpInst::FCMP_OGT, lhs, rhs);
	}
	else if (is_signed)
	{
		lower_than   = ir.CreateICmp(llvm::CmpInst::ICMP_SLT, lhs, rhs);
		greater_than = ir.CreateICmp(llvm::CmpInst::ICMP_SGT, lhs, rhs);
	}
	else
	{
		lower_than   = ir.CreateICmp(llvm::CmpInst::ICMP_ULT, lhs, rhs);
		greater_than = ir.CreateICmp(llvm::CmpInst::ICMP_UGT, lhs, rhs);
	}

	// lt_or_eq = lhs < rhs ? -1 : 0;
	llvm::Value* lt_or_eq = ir.CreateSelect(lower_than, constant_lt, constant_eq);

	// cmp = lhs > rhs ? 1 : lt_or_eq
	// cmp = lhs > rhs ? 1 : (lhs < rhs ? -1 : 0)
	llvm::Value* cmp = ir.CreateSelect(greater_than, constant_gt, lt_or_eq);

	store_value_register_value(cmp);
}

void FunctionBuilder::emit_system_call(const asCScriptFunction& function)
{
	Builder&                    builder = m_context.compiler->builder();
	llvm::IRBuilder<>&          ir      = builder.ir();
	StandardTypes&              types   = builder.standard_types();
	StandardFunctions&          funcs   = m_context.module_builder->standard_functions();
	asSSystemFunctionInterface& intf    = *function.sysFuncIntf;

	llvm::FunctionType* callee_type = m_context.module_builder->get_system_function_type(function);

	const std::size_t         argument_count = callee_type->getNumParams();
	std::vector<llvm::Value*> args(argument_count);

	// TODO: should use references were possible, *const for consistency for now though
	llvm::Type* const return_type    = callee_type->getReturnType();
	llvm::Value*      return_pointer = nullptr;
	llvm::Value*      object         = nullptr;

	if (function.DoesReturnOnStack() && !intf.hostReturnInMemory)
	{
		return_pointer = m_stack.pop(AS_PTR_SIZE, return_type->getPointerTo());
	}

	const auto pop_sret_pointer = [&] {
		llvm::Type*  param_type = callee_type->getParamType(0);
		llvm::Value* value      = m_stack.pop(AS_PTR_SIZE, param_type);
		return value;
	};

	for (std::size_t i = 0, insert_index = 0, script_param_index = 0; i < argument_count; ++i)
	{
		const auto pop_param = [&] {
			llvm::Type* param_type = callee_type->getParamType(insert_index);

			args[insert_index]
				= m_stack.pop(function.parameterTypes[script_param_index].GetSizeOnStackDWords(), param_type);

			++insert_index;
			++script_param_index;
		};

		switch (intf.callConv)
		{
		case ICC_VIRTUAL_THISCALL:
		case ICC_THISCALL:
		case ICC_CDECL_OBJFIRST:
		{
			if (intf.hostReturnInMemory)
			{
				// 'this' pointer
				if (i == 0)
				{
					args[1] = object = m_stack.pop(AS_PTR_SIZE, callee_type->getParamType(1));
					break;
				}

				if (i == 1)
				{
					args[0]      = pop_sret_pointer();
					insert_index = 2;
					break;
				}
			}
			else
			{
				// 'this' pointer
				if (i == 0)
				{
					args[0] = object = m_stack.pop(AS_PTR_SIZE, callee_type->getParamType(0));
					insert_index     = 1;
					break;
				}
			}

			pop_param();

			break;
		}

		case ICC_CDECL_OBJLAST:
		{
			// 'this' pointer
			if (i == 0)
			{
				args.back() = object
					= m_stack.pop(AS_PTR_SIZE, callee_type->getParamType(callee_type->getNumParams() - 1));
				break;
			}

			if (intf.hostReturnInMemory && i == 1)
			{
				args[0]      = pop_sret_pointer();
				insert_index = 1;
				break;
			}

			pop_param();

			break;
		}

		case ICC_CDECL:
		{
			pop_param();
			break;
		}

		default:
		{
			asllvm_assert(false && "unhandled calling convention");
		}
		}
	}

	llvm::Value* callee = nullptr;

	switch (intf.callConv)
	{
	// Virtual
	case ICC_VIRTUAL_THISCALL:
	{
		asllvm_assert(object != nullptr);

		std::array<llvm::Value*, 2> lookup_args{
			object,
			ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, reinterpret_cast<asPWORD>(intf.func)), types.pvoid)};

		callee
			= ir.CreatePointerCast(ir.CreateCall(funcs.system_vtable_lookup, lookup_args), callee_type->getPointerTo());

		break;
	}

	default:
	{
		callee = m_context.module_builder->get_system_function(function);
		break;
	}
	}

	llvm::Value* result = ir.CreateCall(callee_type, callee, args);

	if (return_pointer == nullptr)
	{
		if (return_type != types.tvoid)
		{
			if (function.returnType.IsObjectHandle())
			{
				llvm::Value* typed_object_register
					= ir.CreatePointerCast(m_object_register, return_type->getPointerTo());
				ir.CreateStore(result, typed_object_register);
			}
			else
			{
				store_value_register_value(result);
			}
		}
	}
	else
	{
		ir.CreateStore(result, return_pointer);
	}

	// Factory functions pop into the parameters, not just the temporary stack.
	// Our checks expect the stack pointer to be within variable_space() <= sp, so this would cause issues otherwise.
	m_stack.ugly_hack_stack_pointer_within_bounds();
}

std::size_t FunctionBuilder::emit_script_call(const asCScriptFunction& callee) { return emit_script_call(callee, {}); }

std::size_t FunctionBuilder::emit_script_call(const asCScriptFunction& callee, FunctionBuilder::VmEntryCallContext ctx)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	const bool is_vm_entry = ctx.vm_frame_pointer != nullptr;

	// Check supported calls
	switch (callee.funcType)
	{
	case asFUNC_VIRTUAL:
	case asFUNC_SCRIPT: break;
	default: asllvm_assert(false && "unsupported script type for script function call");
	}

	llvm::FunctionType* callee_type = m_context.module_builder->get_script_function_type(callee);

	llvm::Value* resolved_function = nullptr;

	if (callee.funcType == asFUNC_VIRTUAL)
	{
		resolved_function = resolve_virtual_script_function(m_stack.top(types.pvoid), callee);
	}
	else
	{
		resolved_function = m_context.module_builder->get_script_function(callee);
	}

	std::size_t read_dword_count = 0;

	const auto pop = [&](const asCDataType& type) -> llvm::Value* {
		const std::size_t old_read_dword_count = read_dword_count;
		const std::size_t object_dword_size    = type.GetSizeOnStackDWords();
		read_dword_count += object_dword_size;

		llvm::Type* llvm_parameter_type = m_context.compiler->builder().to_llvm_type(type);

		if (is_vm_entry)
		{
			const std::array<llvm::Value*, 1> offsets{llvm::ConstantInt::get(types.iptr, -long(old_read_dword_count))};
			llvm::Value*                      dword_pointer = ir.CreateInBoundsGEP(ctx.vm_frame_pointer, offsets);

			llvm::Value* pointer = ir.CreatePointerCast(dword_pointer, llvm_parameter_type->getPointerTo());
			return ir.CreateLoad(llvm_parameter_type, pointer);
		}

		llvm::Value* value = m_stack.pop(object_dword_size, llvm_parameter_type);
		return value;
	};

	std::vector<llvm::Value*> args;

	if (callee.DoesReturnOnStack())
	{
		args.push_back(pop(callee.returnType));
	}

	if (callee.GetObjectType() != nullptr)
	{
		args.push_back(pop(m_context.compiler->engine().GetDataTypeFromTypeId(callee.objectType->GetTypeId())));
	}

	{
		const std::size_t parameter_count = callee.parameterTypes.GetLength();
		for (std::size_t i = 0; i < parameter_count; ++i)
		{
			args.push_back(pop(callee.parameterTypes[i]));
		}
	}

	llvm::CallInst* ret = ir.CreateCall(callee_type, resolved_function, args);

	if (callee.returnType.GetTokenType() != ttVoid && !callee.DoesReturnOnStack())
	{
		if (callee.returnType.IsObjectHandle() || callee.returnType.IsObject())
		{
			// Store to the object register
			llvm::Value* typed_object_register = ir.CreatePointerCast(
				is_vm_entry ? ctx.object_register : m_object_register, ret->getType()->getPointerTo());

			ir.CreateStore(ret, typed_object_register);
		}
		else if (callee.returnType.IsPrimitive())
		{
			// TODO: code is similar with the above branch

			// Store to the value register
			llvm::Value* typed_value_register = ir.CreatePointerCast(
				is_vm_entry ? ctx.value_register : m_value_register, ret->getType()->getPointerTo());

			ir.CreateStore(ret, typed_value_register);
		}
		else
		{
			asllvm_assert(false && "unhandled return type");
		}
	}

	return read_dword_count;
}

void FunctionBuilder::emit_call(const asCScriptFunction& function)
{
	switch (function.funcType)
	{
	case asFUNC_SCRIPT:
	case asFUNC_VIRTUAL:
	case asFUNC_DELEGATE:
	{
		emit_script_call(function);
		break;
	}

	case asFUNC_SYSTEM:
	{
		emit_system_call(function);
		break;
	}

	default:
	{
		asllvm_assert(false && "unhandled function type in emit_call");
		break;
	}
	}
}

void FunctionBuilder::emit_object_method_call(const asCScriptFunction& function, llvm::Value* object)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();
	StandardFunctions& funcs   = m_context.module_builder->standard_functions();

	std::array<llvm::Value*, 2> args{
		object,
		ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, reinterpret_cast<asPWORD>(&function)), types.pvoid)};

	ir.CreateCall(funcs.call_object_method, args);
}

void FunctionBuilder::emit_conditional_branch(BytecodeInstruction ins, llvm::CmpInst::Predicate predicate)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	llvm::Value* condition
		= ir.CreateICmp(predicate, load_value_register_value(types.i32), llvm::ConstantInt::get(types.i32, 0));

	ir.CreateCondBr(condition, get_branch_target(ins), get_conditional_fail_branch_target(ins));
}

llvm::Value*
FunctionBuilder::resolve_virtual_script_function(llvm::Value* script_object, const asCScriptFunction& callee)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	// FIXME: check script_object pointer and raise VM exception TXT_NULL_POINTER_ACCESS when null

	const bool is_final = callee.IsFinal() || ((callee.objectType->flags & asOBJ_NOINHERIT) != 0);

	if (m_context.compiler->config().allow_devirtualization && is_final)
	{
		asCScriptFunction* resolved_script_function = get_nonvirtual_match(callee);
		asllvm_assert(resolved_script_function != nullptr);

		return m_context.module_builder->get_script_function(*resolved_script_function);
	}
	else
	{
		llvm::Value* function_value = ir.CreateIntToPtr(
			llvm::ConstantInt::get(types.iptr, reinterpret_cast<asPWORD>(&callee)),
			types.pvoid,
			"virtual_script_function");

		llvm::FunctionCallee        lookup = m_context.module_builder->standard_functions().script_vtable_lookup;
		std::array<llvm::Value*, 2> lookup_args{{script_object, function_value}};

		return ir.CreatePointerCast(
			ir.CreateCall(lookup, lookup_args),
			m_context.module_builder->get_script_function_type(callee)->getPointerTo(),
			"resolved_vcall");
	}
}

void FunctionBuilder::store_value_register_value(llvm::Value* value)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	ir.CreateStore(value, get_value_register_pointer(value->getType()));
}

llvm::Value* FunctionBuilder::load_value_register_value(llvm::Type* type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	return ir.CreateLoad(type, get_value_register_pointer(type));
}

llvm::Value* FunctionBuilder::get_value_register_pointer(llvm::Type* type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	return ir.CreatePointerCast(m_value_register, type->getPointerTo());
}

void FunctionBuilder::insert_label(long offset)
{
	llvm::LLVMContext& context = *m_context.compiler->builder().llvm_context().getContext(); // long boi

	auto       emplace_result = m_jump_map.emplace(offset, nullptr);
	const bool success        = emplace_result.second;

	if (!success)
	{
		// there was a label here already; no need to recreate one
		return;
	}

	auto it    = emplace_result.first;
	it->second = llvm::BasicBlock::Create(context, fmt::format("branch_to_{:04x}", offset), m_context.llvm_function);
}

void FunctionBuilder::preprocess_conditional_branch(BytecodeInstruction instruction)
{
	insert_label(instruction.offset + 2);
	preprocess_unconditional_branch(instruction);
}

void FunctionBuilder::preprocess_unconditional_branch(BytecodeInstruction instruction)
{
	insert_label(instruction.offset + 2 + instruction.arg_int());
}

llvm::BasicBlock* FunctionBuilder::get_branch_target(BytecodeInstruction instruction)
{
	return m_jump_map.at(instruction.offset + 2 + instruction.arg_int());
}

llvm::BasicBlock* FunctionBuilder::get_conditional_fail_branch_target(BytecodeInstruction instruction)
{
	return m_jump_map.at(instruction.offset + 2);
}

void FunctionBuilder::switch_to_block(llvm::BasicBlock* block)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();

	if (auto* current_terminator = ir.GetInsertBlock()->getTerminator(); current_terminator == nullptr)
	{
		// The current block has no terminator: insert a branch to the current one, which immediately follows it.
		ir.CreateBr(block);
	}

	ir.SetInsertPoint(block);
}

void FunctionBuilder::create_function_debug_info(llvm::Function* function, GeneratedFunctionType type)
{
	asCScriptEngine&   engine            = m_context.compiler->engine();
	llvm::IRBuilder<>& ir                = m_context.compiler->builder().ir();
	llvm::DIBuilder&   di                = m_context.module_builder->di_builder();
	ModuleDebugInfo&   module_debug_info = m_context.module_builder->debug_info();

	std::vector<llvm::Metadata*> types;
	{
		types.push_back(m_context.module_builder->get_debug_type(m_context.script_function->GetReturnTypeId()));

		if (m_context.script_function->DoesReturnOnStack())
		{
			types.push_back(m_context.module_builder->get_debug_type(
				engine.GetTypeIdFromDataType(m_context.script_function->returnType)));
		}

		if (m_context.script_function->objectType != nullptr)
		{
			types.push_back(m_context.module_builder->get_debug_type(m_context.script_function->objectType->typeId));
		}

		const std::size_t count = m_context.script_function->GetParamCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			int type_id = 0;
			m_context.script_function->GetParam(i, &type_id);
			types.push_back(m_context.module_builder->get_debug_type(type_id));
		}
	}

	llvm::DIFile* file = di.createFile(m_context.script_function->GetScriptSectionName(), ".");

	const auto location = get_source_location(m_context);

	std::string_view symbol_suffix;

	switch (type)
	{
	case GeneratedFunctionType::Implementation: break;
	case GeneratedFunctionType::VmEntryThunk: symbol_suffix = "!vmthunk"; break;
	}

	llvm::DISubprogram* sp = di.createFunction(
		module_debug_info.compile_unit,
		fmt::format("{}{}", make_debug_name(*m_context.script_function), symbol_suffix),
		llvm::StringRef{},
		file,
		location.line,
		di.createSubroutineType(di.getOrCreateTypeArray(types)),
		location.line,
		llvm::DINode::FlagPrototyped | llvm::DINode::FlagThunk,
		llvm::DISubprogram::SPFlagDefinition);

	function->setSubprogram(sp);

	ir.SetCurrentDebugLocation(get_debug_location(m_context, 0, sp));
}

llvm::Value* FunctionBuilder::load_global(asPWORD address, llvm::Type* type)
{
	Builder&           builder = m_context.compiler->builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	StandardTypes&     types   = builder.standard_types();

	llvm::Value* global_address = ir.CreateIntToPtr(llvm::ConstantInt::get(types.iptr, address), types.pi32);
	return ir.CreateLoad(type, global_address);
}
} // namespace asllvm::detail
