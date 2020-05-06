#include <angelscript-llvm/detail/functionbuilder.hpp>

#include <angelscript-llvm/detail/ashelper.hpp>
#include <angelscript-llvm/detail/asinternalheaders.hpp>
#include <angelscript-llvm/detail/assert.hpp>
#include <angelscript-llvm/detail/jitcompiler.hpp>
#include <angelscript-llvm/detail/llvmglobals.hpp>
#include <angelscript-llvm/detail/modulebuilder.hpp>
#include <angelscript-llvm/detail/modulecommon.hpp>
#include <array>
#include <fmt/core.h>

namespace asllvm::detail
{
FunctionBuilder::FunctionBuilder(
	JitCompiler&       compiler,
	ModuleBuilder&     module_builder,
	asCScriptFunction& script_function,
	llvm::Function*    llvm_function) :
	m_compiler{compiler},
	m_module_builder{module_builder},
	m_script_function{script_function},
	m_llvm_function{llvm_function},
	m_stack{codegen::Context{&m_compiler.builder(), m_llvm_function, &m_script_function}} // TODO: common context
{}

llvm::Function* FunctionBuilder::translate_bytecode(asDWORD* bytecode, asUINT length)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	llvm::orc::ThreadSafeContext& thread_safe_context = m_compiler.builder().context();
	auto                          context_lock        = thread_safe_context.getLock();
	auto&                         context             = *thread_safe_context.getContext();

	ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", m_llvm_function));

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
		if (m_compiler.config().verbose)
		{
			fmt::print(stderr, "Function {}\n", m_script_function.GetDeclaration(true, true, true));
			fmt::print(
				stderr,
				"scriptData.variableSpace: {}\n"
				"scriptData.stackNeeded: {}\n",
				m_script_function.scriptData->variableSpace,
				m_script_function.scriptData->stackNeeded);

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

		create_function_debug_info(m_llvm_function, GeneratedFunctionType::Implementation);
		emit_allocate_local_structures();
		create_locals_debug_info();

		walk_bytecode([&](BytecodeInstruction instruction) {
			translate_instruction(instruction);

			// Emit metadata on last inserted instruction for debugging
			if (m_compiler.config().verbose)
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
		m_llvm_function->removeFromParent();
		throw;
	}

	return m_llvm_function;
}

llvm::Function* FunctionBuilder::create_vm_entry_thunk()
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	llvm::orc::ThreadSafeContext& thread_safe_context = m_compiler.builder().context();
	auto                          context_lock        = thread_safe_context.getLock();
	auto&                         context             = *thread_safe_context.getContext();

	const std::array<llvm::Type*, 2> types{defs.vm_registers->getPointerTo(), defs.i64};

	llvm::Type* return_type = defs.tvoid;

	llvm::Function* wrapper_function = llvm::Function::Create(
		llvm::FunctionType::get(return_type, types, false),
		llvm::Function::ExternalLinkage,
		make_vm_entry_thunk_name(m_script_function),
		m_module_builder.module());

	create_function_debug_info(wrapper_function, GeneratedFunctionType::VmEntryThunk);

	llvm::BasicBlock* block = llvm::BasicBlock::Create(context, "entry", wrapper_function);

	ir.SetInsertPoint(block);

	llvm::Argument* registers = &*(wrapper_function->arg_begin() + 0);
	registers->setName("vmregs");

	llvm::Argument* arg = &*(wrapper_function->arg_begin() + 1);
	arg->setName("jitarg");

	llvm::Value* frame_pointer = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(defs.i64, 0), llvm::ConstantInt::get(defs.i32, 1)};

		auto* pointer = ir.CreateGEP(registers, indices, "stackFramePointerPointer");
		return ir.CreateLoad(defs.pi32, pointer, "stackFramePointer");
	}();

	llvm::Value* value_register = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(defs.i64, 0), llvm::ConstantInt::get(defs.i32, 3)};
		return ir.CreateGEP(registers, indices, "valueRegister");
	}();

	llvm::Value* object_register = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(defs.i64, 0), llvm::ConstantInt::get(defs.i32, 4)};

		auto* pointer = ir.CreateGEP(registers, indices, "objectRegisterPointerPointer");
		return ir.CreateLoad(defs.pvoid, pointer, "objectRegisterPointer");
	}();

	{
		VmEntryCallContext ctx;
		ctx.vm_frame_pointer = frame_pointer;
		ctx.value_register   = value_register;
		ctx.object_register  = object_register;

		emit_script_call(m_script_function, ctx);
	}

	llvm::Value* program_pointer = [&] {
		std::array<llvm::Value*, 2> indices{llvm::ConstantInt::get(defs.i64, 0), llvm::ConstantInt::get(defs.i32, 0)};

		return ir.CreateGEP(registers, indices, "programPointer");
	}();

	// Set the program pointer to the RET instruction
	auto* ret_ptr_value = ir.CreateIntToPtr(
		llvm::ConstantInt::get(defs.i64, reinterpret_cast<std::uintptr_t>(m_ret_pointer)), defs.pi32);
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
	asCScriptEngine&   engine         = m_compiler.engine();
	llvm::IRBuilder<>& ir             = m_compiler.builder().ir();
	CommonDefinitions& defs           = m_compiler.builder().definitions();
	InternalFunctions& internal_funcs = m_module_builder.internal_functions();

	ir.SetCurrentDebugLocation(get_debug_location(ins.offset, m_llvm_function->getSubprogram()));

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
		llvm::Value* pointer_to_global_address
			= ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, ins.arg_pword()), defs.iptr->getPointerTo());
		llvm::Value* global_address = ir.CreateLoad(defs.iptr, pointer_to_global_address);

		m_stack.push(global_address, AS_PTR_SIZE);
		break;
	}

	case asBC_PshC4:
	{
		m_stack.push(llvm::ConstantInt::get(defs.i32, ins.arg_dword()), 1);
		break;
	}

	case asBC_PshV4:
	{
		m_stack.push(m_stack.load(ins.arg_sword0(), defs.i32), 1);
		break;
	}

	case asBC_PSF:
	{
		m_stack.push(m_stack.pointer_to(ins.arg_sword0(), defs.iptr), AS_PTR_SIZE);
		break;
	}

	case asBC_SwapPtr: unimpl(); break;

	case asBC_NOT:
	{
		llvm::Value* source = m_stack.load(ins.arg_sword0(), defs.i1);
		llvm::Value* result
			= ir.CreateSelect(source, llvm::ConstantInt::get(defs.i32, 0), llvm::ConstantInt::get(defs.i32, 1));
		m_stack.store(ins.arg_sword0(), result);
		break;
	}

	case asBC_PshG4:
	{
		// TODO: common code for global_ptr
		llvm::Value* global_ptr = ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, ins.arg_pword()), defs.pi32);
		llvm::Value* global     = ir.CreateLoad(defs.i32, global_ptr);

		m_stack.push(global, 1);
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
		asCDataType& type = m_script_function.returnType;

		if (m_llvm_function->getReturnType() == defs.tvoid)
		{
			ir.CreateRetVoid();
		}
		else if (type.IsObjectHandle() || type.IsObject())
		{
			ir.CreateRet(
				ir.CreateBitCast(ir.CreateLoad(defs.pvoid, m_object_register), m_llvm_function->getReturnType()));
		}
		else
		{
			ir.CreateRet(load_value_register_value(m_llvm_function->getReturnType()));
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
	case asBC_NEGi: emit_neg(ins, defs.i32); break;
	case asBC_NEGf: emit_neg(ins, defs.f32); break;
	case asBC_NEGd: emit_neg(ins, defs.f64); break;
	case asBC_INCi16: emit_increment(defs.i16, 1); break;
	case asBC_INCi8: emit_increment(defs.i8, 1); break;
	case asBC_DECi16: emit_increment(defs.i16, -1); break;
	case asBC_DECi8: emit_increment(defs.i8, -1); break;
	case asBC_INCi: emit_increment(defs.i32, 1); break;
	case asBC_DECi: emit_increment(defs.i32, -1); break;
	case asBC_INCf: emit_increment(defs.f32, 1); break;
	case asBC_DECf: emit_increment(defs.f32, -1); break;
	case asBC_INCd: emit_increment(defs.f64, 1); break;
	case asBC_DECd: emit_increment(defs.f64, -1); break;

	case asBC_IncVi:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), defs.i32);
		llvm::Value* result = ir.CreateAdd(value, llvm::ConstantInt::get(defs.i32, 1));
		m_stack.store(ins.arg_sword0(), result);
		break;
	}

	case asBC_DecVi:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), defs.i32);
		llvm::Value* result = ir.CreateSub(value, llvm::ConstantInt::get(defs.i32, 1));
		m_stack.store(ins.arg_sword0(), result);
		break;
	}

	case asBC_BNOT: emit_bit_not(ins, defs.i32); break;
	case asBC_BAND: emit_binop(ins, llvm::Instruction::And, defs.i32); break;
	case asBC_BOR: emit_binop(ins, llvm::Instruction::Or, defs.i32); break;
	case asBC_BXOR: emit_binop(ins, llvm::Instruction::Xor, defs.i32); break;
	case asBC_BSLL: emit_binop(ins, llvm::Instruction::Shl, defs.i32); break;
	case asBC_BSRL: emit_binop(ins, llvm::Instruction::LShr, defs.i32); break;
	case asBC_BSRA: emit_binop(ins, llvm::Instruction::AShr, defs.i32); break;

	case asBC_COPY: unimpl(); break;

	case asBC_PshC8:
	{
		m_stack.push(llvm::ConstantInt::get(defs.i64, ins.arg_qword()), 2);
		break;
	}

	case asBC_PshVPtr:
	{
		m_stack.push(m_stack.load(ins.arg_sword0(), defs.iptr), AS_PTR_SIZE);
		break;
	}

	case asBC_RDSPtr:
	{
		// Dereference pointer from the top of stack, set the top of the stack to the dereferenced value.

		// TODO: check for null address
		llvm::Value* address = m_stack.top(defs.pvoid->getPointerTo());
		llvm::Value* value   = ir.CreateLoad(defs.pvoid, address);
		m_stack.store(m_stack.current_stack_pointer(), value);
		break;
	}

	case asBC_CMPd:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.f64);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), defs.f64);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPu:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.i32);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), defs.i32);
		emit_compare(lhs, rhs, false);
		break;
	}

	case asBC_CMPf:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.f32);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), defs.f32);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPi:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.i32);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), defs.i32);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPIi:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.i32);
		llvm::Value* rhs = llvm::ConstantInt::get(defs.i32, ins.arg_int());
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPIf:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.f32);
		llvm::Value* rhs = llvm::ConstantInt::get(defs.f32, ins.arg_float());
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPIu:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.i32);
		llvm::Value* rhs = llvm::ConstantInt::get(defs.i32, ins.arg_int());
		emit_compare(lhs, rhs, false);
		break;
	}

	case asBC_JMPP:
	{
		auto& targets = m_switch_map.at(ins.offset);
		asllvm_assert(!targets.empty());

		llvm::SwitchInst* inst
			= ir.CreateSwitch(m_stack.load(ins.arg_sword0(), defs.i32), targets.back(), targets.size());

		for (std::size_t i = 0; i < targets.size(); ++i)
		{
			inst->addCase(llvm::ConstantInt::get(defs.i32, i), targets[i]);
		}

		break;
	}

	case asBC_PopRPtr:
	{
		store_value_register_value(m_stack.pop(AS_PTR_SIZE, defs.iptr));
		break;
	}

	case asBC_PshRPtr:
	{
		m_stack.push(load_value_register_value(defs.iptr), AS_PTR_SIZE);
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
		if (m_compiler.config().verbose)
		{
			m_compiler.diagnostic("Found VM suspend, these are unsupported and ignored", asMSGTYPE_WARNING);
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
				{ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, reinterpret_cast<asPWORD>(&type)), defs.pvoid)}};

			llvm::Value* object_memory_pointer = ir.CreateCall(
				internal_funcs.new_script_object->getFunctionType(), internal_funcs.new_script_object, args);

			// Constructor
			asCScriptFunction& constructor = *static_cast<asCScriptEngine&>(engine).scriptFunctions[constructor_id];

			llvm::Value* target_pointer = m_stack.load(
				m_stack.current_stack_pointer() - constructor.GetSpaceNeededForArguments(), defs.pvoid->getPointerTo());

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
				llvm::ConstantInt::get(defs.iptr, type.size) // TODO: align type.size to 4 bytes
			};

			llvm::Value* object_memory_pointer = ir.CreateCall(
				internal_funcs.alloc->getFunctionType(),
				internal_funcs.alloc,
				alloc_args,
				fmt::format("heap.{}", type.GetName()));

			if (constructor_id != 0)
			{
				m_stack.push(object_memory_pointer, AS_PTR_SIZE);
				emit_system_call(*static_cast<asCScriptEngine&>(engine).scriptFunctions[constructor_id]);
			}

			llvm::Value* target_address = m_stack.pop(AS_PTR_SIZE, defs.pvoid->getPointerTo());

			// FIXME: check for null
			ir.CreateStore(object_memory_pointer, target_address);

			// TODO: check for suspend?
		}

		break;
	}

	case asBC_FREE:
	{
		asCObjectType&    object_type = *reinterpret_cast<asCObjectType*>(ins.arg_pword());
		asSTypeBehaviour& beh         = object_type.beh;

		// TODO: check for null pointer (and ignore if so)

		llvm::Value* variable_pointer = m_stack.pointer_to(ins.arg_sword0(), defs.pvoid);
		llvm::Value* object_pointer   = ir.CreateLoad(defs.pvoid, variable_pointer);

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
				m_compiler.diagnostic("STUB: asOBJ_LIST_PATTERN free. this will result in a leak.", asMSGTYPE_WARNING);
			}

			{
				std::array<llvm::Value*, 1> args{object_pointer};
				ir.CreateCall(internal_funcs.free->getFunctionType(), internal_funcs.free, args);
			}
		}

		break;
	}

	case asBC_LOADOBJ:
	{
		llvm::Value* pointer_to_object = m_stack.load(ins.arg_sword0(), defs.pvoid);
		ir.CreateStore(pointer_to_object, m_object_register);
		m_stack.store(ins.arg_sword0(), ir.CreatePtrToInt(llvm::ConstantInt::get(defs.iptr, 0), defs.pvoid));

		break;
	}

	case asBC_STOREOBJ:
	{
		m_stack.store(ins.arg_sword0(), ir.CreateLoad(defs.pvoid, m_object_register));
		ir.CreateStore(ir.CreatePtrToInt(llvm::ConstantInt::get(defs.iptr, 0), defs.pvoid), m_object_register);
		break;
	}

	case asBC_GETOBJ:
	{
		// Replace a variable index by a pointer to the value

		llvm::Value* offset_pointer = m_stack.pointer_to(m_stack.current_stack_pointer() - ins.arg_word0(), defs.iptr);
		llvm::Value* offset         = ir.CreateLoad(defs.iptr, offset_pointer);

		// Get pointer to where the pointer value on the stack is
		std::array<llvm::Value*, 2> gep_offset{
			llvm::ConstantInt::get(defs.iptr, 0),
			ir.CreateSub(llvm::ConstantInt::get(defs.iptr, m_stack.total_space()), offset, "addr", true, true)};

		llvm::Value* variable_pointer
			= ir.CreateBitCast(ir.CreateGEP(m_stack.storage_alloca(), gep_offset), defs.iptr->getPointerTo());
		llvm::Value* variable = ir.CreateLoad(defs.iptr, variable_pointer);

		ir.CreateStore(variable, offset_pointer);
		ir.CreateStore(llvm::ConstantInt::get(defs.iptr, 0), variable_pointer);
		break;
	}

	case asBC_REFCPY: unimpl(); break;

	case asBC_CHKREF:
	{
		// TODO: check if pointer is null
		break;
	}

	case asBC_GETOBJREF: unimpl(); break;
	case asBC_GETREF:
	{
		llvm::Value* pointer = m_stack.pointer_to(m_stack.current_stack_pointer() - ins.arg_word0(), defs.pi32);

		llvm::Value*                      index = ir.CreateLoad(defs.i32, ir.CreateBitCast(pointer, defs.pi32));
		const std::array<llvm::Value*, 2> indices{
			llvm::ConstantInt::get(defs.i64, 0),
			ir.CreateSub(llvm::ConstantInt::get(defs.i32, m_stack.total_space()), index)};
		llvm::Value* variable_address = ir.CreateGEP(m_stack.storage_alloca(), indices);

		ir.CreateStore(variable_address, ir.CreateBitCast(pointer, defs.pi32->getPointerTo()));

		break;
	}
	case asBC_PshNull: unimpl(); break;
	case asBC_ClrVPtr: unimpl(); break;

	case asBC_OBJTYPE:
	{
		m_stack.push(llvm::ConstantInt::get(defs.iptr, ins.arg_pword()), AS_PTR_SIZE);
		break;
	}

	case asBC_TYPEID: unimpl(); break;

	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:
	{
		m_stack.store(ins.arg_sword0(), llvm::ConstantInt::get(defs.i32, ins.arg_dword()));
		break;
	}

	case asBC_SetV8:
	{
		m_stack.store(ins.arg_sword0(), llvm::ConstantInt::get(defs.i64, ins.arg_qword()));
		break;
	}

	case asBC_ADDSi:
	{
		llvm::Value* stack_pointer = m_stack.pointer_to(m_stack.current_stack_pointer(), defs.iptr);

		// TODO: Check for null pointer
		llvm::Value* original_value = ir.CreateLoad(defs.iptr, stack_pointer);
		llvm::Value* incremented_value
			= ir.CreateAdd(original_value, llvm::ConstantInt::get(defs.iptr, ins.arg_sword0()));

		ir.CreateStore(incremented_value, stack_pointer);
		break;
	}

	case asBC_CpyVtoV4:
	{
		auto target = ins.arg_sword0();
		auto source = ins.arg_sword1();

		m_stack.store(target, m_stack.load(source, defs.i32));

		break;
	}

	case asBC_CpyVtoV8:
	{
		auto target = ins.arg_sword0();
		auto source = ins.arg_sword1();

		m_stack.store(target, m_stack.load(source, defs.i64));

		break;
	}

	case asBC_CpyVtoR4:
	{
		store_value_register_value(m_stack.load(ins.arg_sword0(), defs.i32));
		break;
	}

	case asBC_CpyVtoR8:
	{
		store_value_register_value(m_stack.load(ins.arg_sword0(), defs.i64));
		break;
	}

	case asBC_CpyVtoG4:
	{
		llvm::Value* global_ptr = ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, ins.arg_pword()), defs.pi32);
		llvm::Value* value      = m_stack.load(ins.arg_sword0(), defs.i32);
		ir.CreateStore(value, global_ptr);
		break;
	}

	case asBC_CpyRtoV4:
	{
		m_stack.store(ins.arg_sword0(), load_value_register_value(defs.i32));
		break;
	}

	case asBC_CpyRtoV8:
	{
		m_stack.store(ins.arg_sword0(), load_value_register_value(defs.i64));
		break;
	}

	case asBC_CpyGtoV4:
	{
		llvm::Value* global_ptr = ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, ins.arg_pword()), defs.pi32);
		m_stack.store(ins.arg_sword0(), ir.CreateLoad(global_ptr, defs.i32));
		break;
	}

	case asBC_WRTV1:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), defs.i8);
		llvm::Value* target = load_value_register_value(defs.pi8);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_WRTV2:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), defs.i16);
		llvm::Value* target = load_value_register_value(defs.pi16);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_WRTV4:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), defs.i32);
		llvm::Value* target = load_value_register_value(defs.pi32);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_WRTV8:
	{
		llvm::Value* value  = m_stack.load(ins.arg_sword0(), defs.i64);
		llvm::Value* target = load_value_register_value(defs.pi64);
		ir.CreateStore(value, target);
		break;
	}

	case asBC_RDR1:
	{
		llvm::Value* source_pointer = load_value_register_value(defs.pi8);
		llvm::Value* source_word    = ir.CreateLoad(defs.i8, source_pointer);
		llvm::Value* source         = ir.CreateZExt(source_word, defs.i32);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_RDR2:
	{
		llvm::Value* source_pointer = load_value_register_value(defs.pi16);
		llvm::Value* source_word    = ir.CreateLoad(defs.i16, source_pointer);
		llvm::Value* source         = ir.CreateZExt(source_word, defs.i32);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_RDR4:
	{
		llvm::Value* source_pointer = load_value_register_value(defs.pi32);
		llvm::Value* source         = ir.CreateLoad(defs.i32, source_pointer);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_RDR8:
	{
		llvm::Value* source_pointer = load_value_register_value(defs.pi64);
		llvm::Value* source         = ir.CreateLoad(defs.i64, source_pointer);
		m_stack.store(ins.arg_sword0(), source);
		break;
	}

	case asBC_LDG:
	{
		store_value_register_value(ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, ins.arg_pword()), defs.pvoid));
		break;
	}

	case asBC_LDV:
	{
		store_value_register_value(m_stack.pointer_to(ins.arg_sword0(), defs.pvoid));
		break;
	}

	case asBC_PGA:
	{
		m_stack.push(llvm::ConstantInt::get(defs.i64, ins.arg_pword()), AS_PTR_SIZE);
		break;
	}

	case asBC_CmpPtr: unimpl(); break;

	case asBC_VAR:
	{
		m_stack.push(llvm::ConstantInt::get(defs.i64, ins.arg_sword0()), AS_PTR_SIZE);
		break;
	}

	case asBC_iTOf: emit_cast(ins, llvm::Instruction::SIToFP, defs.i32, defs.f32); break;
	case asBC_fTOi: emit_cast(ins, llvm::Instruction::FPToSI, defs.f32, defs.i32); break;
	case asBC_uTOf: emit_cast(ins, llvm::Instruction::UIToFP, defs.i32, defs.f32); break;
	case asBC_fTOu: emit_cast(ins, llvm::Instruction::FPToUI, defs.f32, defs.i32); break;

	case asBC_sbTOi: emit_cast(ins, llvm::Instruction::SExt, defs.i8, defs.i32); break;
	case asBC_swTOi: emit_cast(ins, llvm::Instruction::SExt, defs.i16, defs.i32); break;
	case asBC_ubTOi: emit_cast(ins, llvm::Instruction::ZExt, defs.i8, defs.i32); break;
	case asBC_uwTOi: emit_cast(ins, llvm::Instruction::ZExt, defs.i16, defs.i32); break;

	case asBC_dTOi: emit_cast(ins, llvm::Instruction::FPToSI, defs.f64, defs.i32); break;
	case asBC_dTOu: emit_cast(ins, llvm::Instruction::FPToUI, defs.f64, defs.i32); break;
	case asBC_dTOf: emit_cast(ins, llvm::Instruction::FPTrunc, defs.f64, defs.f32); break;

	case asBC_iTOd: emit_cast(ins, llvm::Instruction::SIToFP, defs.i32, defs.f64); break;
	case asBC_uTOd: emit_cast(ins, llvm::Instruction::UIToFP, defs.i32, defs.f64); break;
	case asBC_fTOd: emit_cast(ins, llvm::Instruction::FPExt, defs.f32, defs.f64); break;

	case asBC_ADDi: emit_binop(ins, llvm::Instruction::Add, defs.i32); break;
	case asBC_SUBi: emit_binop(ins, llvm::Instruction::Sub, defs.i32); break;
	case asBC_MULi: emit_binop(ins, llvm::Instruction::Mul, defs.i32); break;
	case asBC_DIVi: emit_binop(ins, llvm::Instruction::SDiv, defs.i32); break;
	case asBC_MODi: emit_binop(ins, llvm::Instruction::SRem, defs.i32); break;

	case asBC_ADDf: emit_binop(ins, llvm::Instruction::FAdd, defs.f32); break;
	case asBC_SUBf: emit_binop(ins, llvm::Instruction::FSub, defs.f32); break;
	case asBC_MULf: emit_binop(ins, llvm::Instruction::FMul, defs.f32); break;
	case asBC_DIVf: emit_binop(ins, llvm::Instruction::FDiv, defs.f32); break;
	case asBC_MODf: emit_binop(ins, llvm::Instruction::FRem, defs.f32); break;

	case asBC_ADDd: emit_binop(ins, llvm::Instruction::FAdd, defs.f64); break;
	case asBC_SUBd: emit_binop(ins, llvm::Instruction::FSub, defs.f64); break;
	case asBC_MULd: emit_binop(ins, llvm::Instruction::FMul, defs.f64); break;
	case asBC_DIVd: emit_binop(ins, llvm::Instruction::FDiv, defs.f64); break;
	case asBC_MODd: emit_binop(ins, llvm::Instruction::FRem, defs.f64); break;

	case asBC_ADDIi: emit_binop(ins, llvm::Instruction::Add, llvm::ConstantInt::get(defs.i32, ins.arg_int(1))); break;
	case asBC_SUBIi: emit_binop(ins, llvm::Instruction::Sub, llvm::ConstantInt::get(defs.i32, ins.arg_int(1))); break;
	case asBC_MULIi: emit_binop(ins, llvm::Instruction::Mul, llvm::ConstantInt::get(defs.i32, ins.arg_int(1))); break;

	case asBC_ADDIf: emit_binop(ins, llvm::Instruction::FAdd, llvm::ConstantFP::get(defs.f32, ins.arg_float(1))); break;
	case asBC_SUBIf: emit_binop(ins, llvm::Instruction::FSub, llvm::ConstantFP::get(defs.f32, ins.arg_float(1))); break;
	case asBC_MULIf: emit_binop(ins, llvm::Instruction::FMul, llvm::ConstantFP::get(defs.f32, ins.arg_float(1))); break;

	case asBC_SetG4:
	{
		llvm::Value* pointer = ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, ins.arg_pword()), defs.pi32);
		llvm::Value* value   = llvm::ConstantInt::get(defs.i32, ins.arg_dword(AS_PTR_SIZE));
		ir.CreateStore(value, pointer);
		break;
	}

	case asBC_ChkRefS: unimpl(); break;

	case asBC_ChkNullV:
	{
		// FIXME: Check for null pointer in ChkNullV
		break;
	}

	case asBC_CALLINTF:
	{
		auto& function = static_cast<asCScriptFunction&>(*engine.GetFunctionById(ins.arg_int()));
		emit_script_call(function);
		break;
	}

	case asBC_iTOb: emit_cast(ins, llvm::Instruction::Trunc, defs.i32, defs.i8); break;
	case asBC_iTOw: emit_cast(ins, llvm::Instruction::Trunc, defs.i32, defs.i16); break;

	case asBC_Cast: unimpl(); break;

	case asBC_i64TOi: emit_cast(ins, llvm::Instruction::Trunc, defs.i64, defs.i32); break;
	case asBC_uTOi64: emit_cast(ins, llvm::Instruction::ZExt, defs.i32, defs.i64); break;
	case asBC_iTOi64: emit_cast(ins, llvm::Instruction::SExt, defs.i32, defs.i64); break;

	case asBC_fTOi64: emit_cast(ins, llvm::Instruction::FPToSI, defs.f32, defs.i64); break;
	case asBC_dTOi64: emit_cast(ins, llvm::Instruction::FPToSI, defs.f64, defs.i64); break;
	case asBC_fTOu64: emit_cast(ins, llvm::Instruction::FPToUI, defs.f32, defs.i64); break;
	case asBC_dTOu64: emit_cast(ins, llvm::Instruction::FPToUI, defs.f64, defs.i64); break;

	case asBC_i64TOf: emit_cast(ins, llvm::Instruction::SIToFP, defs.i64, defs.f32); break;
	case asBC_u64TOf: emit_cast(ins, llvm::Instruction::UIToFP, defs.i64, defs.f32); break;
	case asBC_i64TOd: emit_cast(ins, llvm::Instruction::SIToFP, defs.i64, defs.f64); break;
	case asBC_u64TOd: emit_cast(ins, llvm::Instruction::UIToFP, defs.i64, defs.f64); break;

	case asBC_NEGi64: emit_neg(ins, defs.i64); break;
	case asBC_INCi64: emit_increment(defs.i64, 1); break;
	case asBC_DECi64: emit_increment(defs.i64, -1); break;
	case asBC_BNOT64: emit_bit_not(ins, defs.i64); break;

	case asBC_ADDi64: emit_binop(ins, llvm::Instruction::Add, defs.i64); break;
	case asBC_SUBi64: emit_binop(ins, llvm::Instruction::Sub, defs.i64); break;
	case asBC_MULi64: emit_binop(ins, llvm::Instruction::Mul, defs.i64); break;
	case asBC_DIVi64: emit_binop(ins, llvm::Instruction::SDiv, defs.i64); break;
	case asBC_MODi64: emit_binop(ins, llvm::Instruction::SRem, defs.i64); break;

	case asBC_BAND64: emit_binop(ins, llvm::Instruction::And, defs.i64); break;
	case asBC_BOR64: emit_binop(ins, llvm::Instruction::Or, defs.i64); break;
	case asBC_BXOR64: emit_binop(ins, llvm::Instruction::Xor, defs.i64); break;
	case asBC_BSLL64: emit_binop(ins, llvm::Instruction::Shl, defs.i64); break;
	case asBC_BSRL64: emit_binop(ins, llvm::Instruction::LShr, defs.i64); break;
	case asBC_BSRA64: emit_binop(ins, llvm::Instruction::AShr, defs.i64); break;

	case asBC_CMPi64:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.i64);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), defs.i64);
		emit_compare(lhs, rhs);
		break;
	}

	case asBC_CMPu64:
	{
		llvm::Value* lhs = m_stack.load(ins.arg_sword0(), defs.i64);
		llvm::Value* rhs = m_stack.load(ins.arg_sword1(), defs.i64);
		emit_compare(lhs, rhs, false);
		break;
	}

	case asBC_ChkNullS: unimpl(); break;

	case asBC_ClrHi:
	{
		// TODO: untested. What does emit this? Is it possible to see this in final _optimized_ bytecode?
		llvm::Value* source_value = load_value_register_value(defs.i8);
		llvm::Value* extended     = ir.CreateZExt(source_value, defs.i32);
		store_value_register_value(extended);
		break;
	}

	case asBC_JitEntry:
	{
		if (m_compiler.config().verbose)
		{
			m_compiler.diagnostic("Found JIT entry point, patching as valid entry point");
		}

		// Pass the JitCompiler as the jitArg value, which can be used by lazy_jit_compiler().
		// TODO: this is probably UB
		ins.arg_pword() = reinterpret_cast<asPWORD>(&m_compiler);

		break;
	}

	case asBC_CallPtr: unimpl(); break;
	case asBC_FuncPtr: unimpl(); break;

	case asBC_LoadThisR:
	{
		llvm::Value* object = m_stack.load(0, defs.pvoid);

		// TODO: check for null object

		std::array<llvm::Value*, 1> indices{{llvm::ConstantInt::get(defs.iptr, ins.arg_sword0())}};
		llvm::Value*                field = ir.CreateGEP(object, indices);

		store_value_register_value(field);

		break;
	}

	case asBC_PshV8: m_stack.push(m_stack.load(ins.arg_sword0(), defs.i64), 2); break;

	case asBC_DIVu: emit_binop(ins, llvm::Instruction::UDiv, defs.i32); break;
	case asBC_MODu: emit_binop(ins, llvm::Instruction::URem, defs.i32); break;

	case asBC_DIVu64: emit_binop(ins, llvm::Instruction::UDiv, defs.i64); break;
	case asBC_MODu64: emit_binop(ins, llvm::Instruction::URem, defs.i64); break;

	case asBC_LoadRObjR:
	{
		llvm::Value* base_pointer = m_stack.load(ins.arg_sword0(), defs.pvoid);

		// FIXME: check for null base_pointer

		std::array<llvm::Value*, 1> offsets{llvm::ConstantInt::get(defs.iptr, ins.arg_sword1())};
		llvm::Value*                pointer = ir.CreateGEP(base_pointer, offsets, "fieldptr");

		store_value_register_value(pointer);

		break;
	}

	case asBC_LoadVObjR: unimpl(); break;

	case asBC_RefCpyV:
	{
		auto& object_type = *reinterpret_cast<asCObjectType*>(ins.arg_pword());
		// asSTypeBehaviour& beh         = object_type.beh;

		llvm::Value* destination = m_stack.pointer_to(ins.arg_sword0(), defs.pvoid);
		llvm::Value* s           = m_stack.top(defs.pvoid);

		if ((object_type.flags & asOBJ_NOCOUNT) == 0)
		{
			if (object_type.beh.release != 0)
			{
				m_compiler.diagnostic("STUB: asBC_RefCpyV not calling release on old ref!");
			}

			m_compiler.diagnostic("STUB: not checking for zero in addref");

			std::array<llvm::Value*, 2> args{
				s,
				ir.CreateIntToPtr(
					llvm::ConstantInt::get(
						defs.iptr, reinterpret_cast<asPWORD>(engine.GetScriptFunction(object_type.beh.addref))),
					defs.pvoid)};

			ir.CreateCall(
				internal_funcs.call_object_method->getFunctionType(), internal_funcs.call_object_method, args);
		}

		ir.CreateStore(s, destination);

		break;
	}

	case asBC_JLowZ:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_EQ, load_value_register_value(defs.i8), llvm::ConstantInt::get(defs.i8, 0));

		ir.CreateCondBr(condition, get_branch_target(ins), get_conditional_fail_branch_target(ins));

		break;
	}

	case asBC_JLowNZ:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_NE, load_value_register_value(defs.i8), llvm::ConstantInt::get(defs.i8, 0));

		ir.CreateCondBr(condition, get_branch_target(ins), get_conditional_fail_branch_target(ins));

		break;
	}

	case asBC_AllocMem: unimpl(); break;
	case asBC_SetListSize: unimpl(); break;
	case asBC_PshListElmnt: unimpl(); break;
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
		auto* func = static_cast<asCScriptFunction*>(m_compiler.engine().GetFunctionById(instruction.arg_int()));

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
	Builder&           builder = m_compiler.builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	CommonDefinitions& defs    = builder.definitions();

	m_value_register  = ir.CreateAlloca(defs.i64, nullptr, "valuereg");
	m_object_register = ir.CreateAlloca(defs.pvoid, nullptr, "objreg");
	m_stack.setup();
}

void FunctionBuilder::emit_cast(
	BytecodeInstruction        instruction,
	llvm::Instruction::CastOps op,
	llvm::Type*                source_type,
	llvm::Type*                destination_type)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	const bool source_64      = source_type == defs.i64 || source_type == defs.f64;
	const bool destination_64 = destination_type == defs.i64 || destination_type == defs.f64;

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
	llvm::Type* type = rhs->getType();
	return emit_binop(instruction, op, m_stack.load(instruction.arg_sword1(), type), rhs);
}

void FunctionBuilder::emit_binop(
	BytecodeInstruction instruction, llvm::Instruction::BinaryOps op, llvm::Value* lhs, llvm::Value* rhs)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	m_stack.store(instruction.arg_sword0(), ir.CreateBinOp(op, lhs, rhs));
}

void FunctionBuilder::emit_neg(BytecodeInstruction instruction, llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	const bool is_float = type->isFloatingPointTy();

	llvm::Value* lhs    = is_float ? llvm::ConstantFP::get(type, 0.0) : llvm::ConstantInt::get(type, 0);
	llvm::Value* rhs    = m_stack.load(instruction.arg_sword0(), type);
	llvm::Value* result = ir.CreateBinOp(is_float ? llvm::Instruction::FSub : llvm::Instruction::Sub, lhs, rhs);
	m_stack.store(instruction.arg_sword0(), result);
}

void FunctionBuilder::emit_bit_not(BytecodeInstruction instruction, llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	llvm::Value* lhs    = llvm::ConstantInt::get(type, -1);
	llvm::Value* rhs    = m_stack.load(instruction.arg_sword0(), type);
	llvm::Value* result = ir.CreateXor(lhs, rhs);
	m_stack.store(instruction.arg_sword0(), result);
}

void FunctionBuilder::emit_condition(llvm::CmpInst::Predicate pred)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	llvm::Value* lhs    = load_value_register_value(defs.i32);
	llvm::Value* rhs    = llvm::ConstantInt::get(defs.i32, 0);
	llvm::Value* result = ir.CreateICmp(pred, lhs, rhs);
	store_value_register_value(ir.CreateZExt(result, defs.i64));
}

void FunctionBuilder::emit_increment(llvm::Type* value_type, long by)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

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
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	asllvm_assert(lhs->getType() == rhs->getType());

	const bool is_float = lhs->getType()->isFloatingPointTy();

	llvm::Value* constant_lt = llvm::ConstantInt::get(defs.i32, -1);
	llvm::Value* constant_eq = llvm::ConstantInt::get(defs.i32, 0);
	llvm::Value* constant_gt = llvm::ConstantInt::get(defs.i32, 1);

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

void FunctionBuilder::emit_system_call(asCScriptFunction& function)
{
	llvm::IRBuilder<>&          ir             = m_compiler.builder().ir();
	CommonDefinitions&          defs           = m_compiler.builder().definitions();
	InternalFunctions&          internal_funcs = m_module_builder.internal_functions();
	asSSystemFunctionInterface& intf           = *function.sysFuncIntf;

	llvm::FunctionType* callee_type = m_module_builder.get_system_function_type(function);

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
			ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, reinterpret_cast<asPWORD>(intf.func)), defs.pvoid)};

		callee = ir.CreateBitCast(
			ir.CreateCall(
				internal_funcs.system_vtable_lookup->getFunctionType(),
				internal_funcs.system_vtable_lookup,
				lookup_args),
			callee_type->getPointerTo());

		break;
	}

	default:
	{
		callee = m_module_builder.get_system_function(function);
		break;
	}
	}

	llvm::Value* result = ir.CreateCall(callee_type, callee, args);

	if (return_pointer == nullptr)
	{
		if (return_type != defs.tvoid)
		{
			if (function.returnType.IsObjectHandle())
			{
				llvm::Value* typed_object_register = ir.CreateBitCast(m_object_register, return_type->getPointerTo());
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

std::size_t FunctionBuilder::emit_script_call(asCScriptFunction& callee) { return emit_script_call(callee, {}); }

std::size_t FunctionBuilder::emit_script_call(asCScriptFunction& callee, FunctionBuilder::VmEntryCallContext ctx)
{
	Builder&           builder = m_compiler.builder();
	llvm::IRBuilder<>& ir      = builder.ir();
	CommonDefinitions& defs    = builder.definitions();

	const bool is_vm_entry = ctx.vm_frame_pointer != nullptr;

	// Check supported calls
	switch (callee.funcType)
	{
	case asFUNC_VIRTUAL:
	case asFUNC_SCRIPT: break;
	default: asllvm_assert(false && "unsupported script type for script function call");
	}

	llvm::FunctionType* callee_type = m_module_builder.get_script_function_type(callee);

	llvm::Value* resolved_function = nullptr;

	if (callee.funcType == asFUNC_VIRTUAL)
	{
		resolved_function = resolve_virtual_script_function(m_stack.top(defs.pvoid), callee);
	}
	else
	{
		resolved_function = m_module_builder.get_script_function(callee);
	}

	std::size_t read_dword_count = 0;

	const auto pop = [&](const asCDataType& type) -> llvm::Value* {
		const std::size_t old_read_dword_count = read_dword_count;
		const std::size_t object_dword_size    = type.GetSizeOnStackDWords();
		read_dword_count += object_dword_size;

		llvm::Type* llvm_parameter_type = m_compiler.builder().to_llvm_type(type);

		if (is_vm_entry)
		{
			const std::array<llvm::Value*, 1> offsets{llvm::ConstantInt::get(defs.iptr, -long(old_read_dword_count))};
			llvm::Value*                      dword_pointer = ir.CreateGEP(ctx.vm_frame_pointer, offsets);

			llvm::Value* pointer = ir.CreateBitCast(dword_pointer, llvm_parameter_type->getPointerTo());
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
		args.push_back(pop(m_compiler.engine().GetDataTypeFromTypeId(callee.objectType->GetTypeId())));
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
			llvm::Value* typed_object_register = ir.CreateBitCast(
				is_vm_entry ? ctx.object_register : m_object_register, ret->getType()->getPointerTo());

			ir.CreateStore(ret, typed_object_register);
		}
		else if (callee.returnType.IsPrimitive())
		{
			// TODO: code is similar with the above branch

			// Store to the value register
			llvm::Value* typed_value_register
				= ir.CreateBitCast(is_vm_entry ? ctx.value_register : m_value_register, ret->getType()->getPointerTo());

			ir.CreateStore(ret, typed_value_register);
		}
		else
		{
			asllvm_assert(false && "unhandled return type");
		}
	}

	return read_dword_count;
}

void FunctionBuilder::emit_call(asCScriptFunction& function)
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

void FunctionBuilder::emit_object_method_call(asCScriptFunction& function, llvm::Value* object)
{
	llvm::IRBuilder<>& ir             = m_compiler.builder().ir();
	CommonDefinitions& defs           = m_compiler.builder().definitions();
	InternalFunctions& internal_funcs = m_module_builder.internal_functions();

	std::array<llvm::Value*, 2> args{
		object, ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, reinterpret_cast<asPWORD>(&function)), defs.pvoid)};

	ir.CreateCall(internal_funcs.call_object_method->getFunctionType(), internal_funcs.call_object_method, args);
}

void FunctionBuilder::emit_conditional_branch(BytecodeInstruction ins, llvm::CmpInst::Predicate predicate)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	llvm::Value* condition
		= ir.CreateICmp(predicate, load_value_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

	ir.CreateCondBr(condition, get_branch_target(ins), get_conditional_fail_branch_target(ins));
}

llvm::Value* FunctionBuilder::resolve_virtual_script_function(llvm::Value* script_object, asCScriptFunction& callee)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	// FIXME: null check for script_object

	const bool is_final = callee.IsFinal() || ((callee.objectType->flags & asOBJ_NOINHERIT) != 0);

	if (m_compiler.config().allow_devirtualization && is_final)
	{
		asCScriptFunction* resolved_script_function = get_nonvirtual_match(callee);
		asllvm_assert(resolved_script_function != nullptr);

		return m_module_builder.get_script_function(*resolved_script_function);
	}
	else
	{
		llvm::Value* function_value = ir.CreateIntToPtr(
			llvm::ConstantInt::get(defs.iptr, reinterpret_cast<asPWORD>(&callee)),
			defs.pvoid,
			"virtual_script_function");

		llvm::Function*             lookup = m_module_builder.internal_functions().script_vtable_lookup;
		std::array<llvm::Value*, 2> lookup_args{{script_object, function_value}};

		return ir.CreateBitCast(
			ir.CreateCall(lookup->getFunctionType(), lookup, lookup_args),
			m_module_builder.get_script_function_type(callee)->getPointerTo(),
			"resolved_vcall");
	}
}

void FunctionBuilder::store_value_register_value(llvm::Value* value)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	ir.CreateStore(value, get_value_register_pointer(value->getType()));
}

llvm::Value* FunctionBuilder::load_value_register_value(llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	return ir.CreateLoad(type, get_value_register_pointer(type));
}

llvm::Value* FunctionBuilder::get_value_register_pointer(llvm::Type* type)
{
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	return ir.CreateBitCast(m_value_register, type->getPointerTo());
}

void FunctionBuilder::insert_label(long offset)
{
	llvm::LLVMContext& context = *m_compiler.builder().context().getContext();

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
	llvm::IRBuilder<>& ir = m_compiler.builder().ir();

	if (auto* current_terminator = ir.GetInsertBlock()->getTerminator(); current_terminator == nullptr)
	{
		// The current block has no terminator: insert a branch to the current one, which immediately follows it.
		ir.CreateBr(block);
	}

	ir.SetInsertPoint(block);
}

void FunctionBuilder::create_function_debug_info(llvm::Function* function, GeneratedFunctionType type)
{
	asCScriptEngine&   engine            = m_compiler.engine();
	llvm::IRBuilder<>& ir                = m_compiler.builder().ir();
	llvm::DIBuilder&   di                = m_module_builder.di_builder();
	ModuleDebugInfo&   module_debug_info = m_module_builder.debug_info();

	std::vector<llvm::Metadata*> types;
	{
		types.push_back(m_module_builder.get_debug_type(m_script_function.GetReturnTypeId()));

		if (m_script_function.DoesReturnOnStack())
		{
			types.push_back(
				m_module_builder.get_debug_type(engine.GetTypeIdFromDataType(m_script_function.returnType)));
		}

		if (m_script_function.objectType != nullptr)
		{
			types.push_back(m_module_builder.get_debug_type(m_script_function.objectType->typeId));
		}

		const std::size_t count = m_script_function.GetParamCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			int type_id = 0;
			m_script_function.GetParam(i, &type_id);
			types.push_back(m_module_builder.get_debug_type(type_id));
		}
	}

	llvm::DIFile* file = di.createFile(m_script_function.GetScriptSectionName(), ".");

	const SourceLocation loc = get_source_location();

	std::string_view symbol_suffix;

	switch (type)
	{
	case GeneratedFunctionType::Implementation: break;
	case GeneratedFunctionType::VmEntryThunk: symbol_suffix = "!vmthunk"; break;
	}

	llvm::DISubprogram* sp = di.createFunction(
		module_debug_info.compile_unit,
		fmt::format("{}{}", make_debug_name(m_script_function), symbol_suffix),
		llvm::StringRef{},
		file,
		loc.line,
		di.createSubroutineType(di.getOrCreateTypeArray(types)),
		loc.line,
		llvm::DINode::FlagPrototyped | llvm::DINode::FlagThunk,
		llvm::DISubprogram::SPFlagDefinition);

	function->setSubprogram(sp);

	ir.SetCurrentDebugLocation(get_debug_location(0, sp));
}

void FunctionBuilder::create_locals_debug_info()
{
	asCScriptEngine&   engine = m_compiler.engine();
	llvm::IRBuilder<>& ir     = m_compiler.builder().ir();
	llvm::DIBuilder&   di     = m_module_builder.di_builder();

	llvm::DISubprogram* sp = m_llvm_function->getSubprogram();

	/*for (const auto& [stack_offset, param] : m_parameters)
	{
		llvm::DILocalVariable* local = di.createParameterVariable(
			sp,
			param.debug_name,
			param.argument_index,
			sp->getFile(),
			0,
			m_module_builder.get_debug_type(param.type_id));

		di.insertDeclare(
			param.local_alloca, local, di.createExpression(), get_debug_location(0, sp), ir.GetInsertBlock());
	}

	{
		const auto& vars = m_script_function.scriptData->variables;
		for (std::size_t i = m_script_function.GetParamCount(); i < vars.GetLength(); ++i)
		{
			const auto& var = vars[i];

			llvm::DILocalVariable* local = di.createAutoVariable(
				sp,
				&var->name[0],
				sp->getFile(),
				0,
				m_module_builder.get_debug_type(engine.GetTypeIdFromDataType(var->type)));

			std::array<std::uint64_t, 2> addresses{
				llvm::dwarf::DW_OP_plus_uconst,
				std::uint64_t(stack_size() + local_storage_size() - var->stackOffset) * 4};

			di.insertDeclare(
				m_locals, local, di.createExpression(addresses), get_debug_location(0, sp), ir.GetInsertBlock());
		}
	}*/
}

FunctionBuilder::SourceLocation FunctionBuilder::get_source_location(std::size_t bytecode_offset)
{
	int       section;
	const int encoded_line = m_script_function.GetLineNumber(bytecode_offset, &section);

	const int line = encoded_line & 0xFFFFF, column = encoded_line >> 20;
	return {line, column};
}

llvm::DebugLoc FunctionBuilder::get_debug_location(std::size_t bytecode_offset, llvm::DISubprogram* sp)
{
	const SourceLocation loc = get_source_location(bytecode_offset);
	return llvm::DebugLoc::get(loc.line, loc.column, sp);
}

} // namespace asllvm::detail
