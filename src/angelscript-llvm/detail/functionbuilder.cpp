#include <angelscript-llvm/detail/functionbuilder.hpp>

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
	m_llvm_function{llvm_function}
{
	m_compiler.builder().ir().SetInsertPoint(
		llvm::BasicBlock::Create(m_compiler.builder().context(), "entry", llvm_function));
}

llvm::Function* FunctionBuilder::read_bytecode(asDWORD* bytecode, asUINT length)
{
	llvm::IRBuilder<>& ir      = m_compiler.builder().ir();
	llvm::LLVMContext& context = m_compiler.builder().context();

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
		if (m_compiler.config().verbose)
		{
			fmt::print(stderr, "Bytecode disassembly: \n");
			walk_bytecode([this](InstructionContext instruction) {
				const std::string op = disassemble(instruction);
				if (!op.empty())
				{
					fmt::print(stderr, "{:04x}: {}\n", instruction.offset, op);
				}
			});
			fmt::print(stderr, "\n");
		}

		walk_bytecode([this](InstructionContext instruction) { preprocess_instruction(instruction); });

		emit_allocate_local_structures();

		walk_bytecode([&](InstructionContext instruction) {
			process_instruction(instruction);

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

		asllvm_assert(m_stack_pointer == m_locals_size);
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
		make_jit_entry_name(m_script_function.GetName(), m_script_function.GetNamespace()),
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

	default: break;
	}
}

void FunctionBuilder::process_instruction(InstructionContext instruction)
{
	asIScriptEngine&   engine         = m_compiler.engine();
	llvm::IRBuilder<>& ir             = m_compiler.builder().ir();
	CommonDefinitions& defs           = m_compiler.builder().definitions();
	InternalFunctions& internal_funcs = m_module_builder.internal_functions();

	const auto old_stack_pointer = m_stack_pointer;

	if (auto it = m_jump_map.find(instruction.offset); it != m_jump_map.end())
	{
		asllvm_assert(m_stack_pointer == m_locals_size);
		switch_to_block(it->second);
	}

	// Ensure that the stack pointer is within bounds
	asllvm_assert(m_stack_pointer >= m_locals_size);
	asllvm_assert(m_stack_pointer <= m_locals_size + m_max_extra_stack_size);

	const auto unimpl = [] { asllvm_assert(false && "unimplemented instruction while translating bytecode"); };

	// TODO: handle division by zero by setting an exception on the context

	switch (instruction.info->bc)
	{
	case asBC_PopPtr:
	{
		m_stack_pointer -= AS_PTR_SIZE;
		break;
	}

	case asBC_PshGPtr: unimpl(); break;

	case asBC_PshC4:
	{
		++m_stack_pointer;
		store_stack_value(m_stack_pointer, llvm::ConstantInt::get(defs.i32, asBC_DWORDARG(instruction.pointer)));
		break;
	}

	case asBC_PshV4:
	{
		++m_stack_pointer;
		store_stack_value(m_stack_pointer, load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32));
		break;
	}

	case asBC_PSF:
	{
		m_stack_pointer += AS_PTR_SIZE;
		llvm::Value* ptr = get_stack_value_pointer(asBC_SWORDARG0(instruction.pointer), defs.iptr);
		store_stack_value(m_stack_pointer, ptr);
		break;
	}

	case asBC_SwapPtr: unimpl(); break;
	case asBC_NOT: unimpl(); break;
	case asBC_PshG4:
	{
		++m_stack_pointer;
		// TODO: common code for global_ptr
		llvm::Value* global_ptr
			= ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, asBC_PTRARG(instruction.pointer)), defs.pi32);
		llvm::Value* global = ir.CreateLoad(defs.i32, global_ptr);
		store_stack_value(m_stack_pointer, global);
		break;
	}

	case asBC_LdGRdR4: unimpl(); break;

	case asBC_CALL:
	{
		auto& function = static_cast<asCScriptFunction&>(*engine.GetFunctionById(asBC_INTARG(instruction.pointer)));
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
		else if (type.IsObjectHandle())
		{
			ir.CreateRet(ir.CreateLoad(defs.pvoid, m_object_register));
		}
		else
		{
			ir.CreateRet(load_value_register_value(m_llvm_function->getReturnType()));
		}

		m_ret_pointer = instruction.pointer;
		break;
	}

	case asBC_JMP:
	{
		ir.CreateBr(get_branch_target(instruction));
		break;
	}

	case asBC_JZ:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_EQ, load_value_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

		ir.CreateCondBr(condition, get_branch_target(instruction), get_conditional_fail_branch_target(instruction));

		break;
	}

	case asBC_JNZ:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_NE, load_value_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

		ir.CreateCondBr(condition, get_branch_target(instruction), get_conditional_fail_branch_target(instruction));

		break;
	}

	case asBC_JS:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_SLT, load_value_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

		ir.CreateCondBr(condition, get_branch_target(instruction), get_conditional_fail_branch_target(instruction));

		break;
	}

	case asBC_JNS:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_SGE, load_value_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

		ir.CreateCondBr(condition, get_branch_target(instruction), get_conditional_fail_branch_target(instruction));

		break;
	}

	case asBC_JP:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_SGT, load_value_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

		ir.CreateCondBr(condition, get_branch_target(instruction), get_conditional_fail_branch_target(instruction));

		break;
	}

	case asBC_JNP:
	{
		llvm::Value* condition = ir.CreateICmp(
			llvm::CmpInst::ICMP_SLE, load_value_register_value(defs.i32), llvm::ConstantInt::get(defs.i32, 0));

		ir.CreateCondBr(condition, get_branch_target(instruction), get_conditional_fail_branch_target(instruction));

		break;
	}

	case asBC_TZ: unimpl(); break;
	case asBC_TNZ: unimpl(); break;
	case asBC_TS: unimpl(); break;
	case asBC_TNS: unimpl(); break;
	case asBC_TP: unimpl(); break;
	case asBC_TNP: unimpl(); break;
	case asBC_NEGi: unimpl(); break;
	case asBC_NEGf: unimpl(); break;
	case asBC_NEGd: unimpl(); break;
	case asBC_INCi16: unimpl(); break;
	case asBC_INCi8: unimpl(); break;
	case asBC_DECi16: unimpl(); break;
	case asBC_DECi8: unimpl(); break;
	case asBC_INCi: unimpl(); break;
	case asBC_INCf: unimpl(); break;
	case asBC_DECf: unimpl(); break;
	case asBC_INCd: unimpl(); break;
	case asBC_DECd: unimpl(); break;

	case asBC_IncVi:
	{
		llvm::Value* value  = load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32);
		llvm::Value* result = ir.CreateAdd(value, llvm::ConstantInt::get(defs.i32, 1));
		store_stack_value(asBC_SWORDARG0(instruction.pointer), result);
		break;
	}

	case asBC_DecVi:
	{
		llvm::Value* value  = load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32);
		llvm::Value* result = ir.CreateSub(value, llvm::ConstantInt::get(defs.i32, 1));
		store_stack_value(asBC_SWORDARG0(instruction.pointer), result);
		break;
	}

	case asBC_BNOT: unimpl(); break;

	case asBC_BAND:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::And, defs.i32);
		break;
	}

	case asBC_BOR:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Or, defs.i32);
		break;
	}

	case asBC_BXOR:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Xor, defs.i32);
		break;
	}

	case asBC_BSLL:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Shl, defs.i32);
		break;
	}

	case asBC_BSRL:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::LShr, defs.i32);
		break;
	}

	case asBC_BSRA:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::AShr, defs.i32);
		break;
	}

	case asBC_COPY: unimpl(); break;

	case asBC_PshC8:
	{
		m_stack_pointer += 2;
		store_stack_value(m_stack_pointer, llvm::ConstantInt::get(defs.i64, asBC_QWORDARG(instruction.pointer)));
		break;
	}

	case asBC_PshVPtr:
	{
		m_stack_pointer += AS_PTR_SIZE;
		store_stack_value(m_stack_pointer, load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.iptr));
		break;
	}

	case asBC_RDSPtr: unimpl(); break;
	case asBC_CMPd: unimpl(); break;
	case asBC_CMPu: unimpl(); break;
	case asBC_CMPf: unimpl(); break;

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

	case asBC_CMPIf: unimpl(); break;
	case asBC_CMPIu: unimpl(); break;
	case asBC_JMPP: unimpl(); break;
	case asBC_PopRPtr: unimpl(); break;
	case asBC_PshRPtr: unimpl(); break;
	case asBC_STR: asllvm_assert(false && "STR is deperecated and should not have been emitted by AS"); break;

	case asBC_CALLSYS:
	{
		asCScriptFunction* function
			= static_cast<asCScriptFunction*>(engine.GetFunctionById(asBC_INTARG(instruction.pointer)));

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
		auto&     type           = *reinterpret_cast<asCObjectType*>(asBC_PTRARG(instruction.pointer));
		const int constructor_id = asBC_INTARG(instruction.pointer + AS_PTR_SIZE);

		if (type.flags & asOBJ_SCRIPT_OBJECT)
		{
			// Allocate memory for the object
			std::array<llvm::Value*, 1> alloc_args{{
				llvm::ConstantInt::get(defs.iptr, type.size) // TODO: align type.size to 4 bytes
			}};

			llvm::Value* object_memory_pointer
				= ir.CreateCall(internal_funcs.alloc->getFunctionType(), internal_funcs.alloc, alloc_args);

			// Initialize stuff using the scriptobject constructor
			std::array<llvm::Value*, 2> scriptobject_constructor_args{
				{ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, reinterpret_cast<asPWORD>(&type)), defs.pvoid),
				 object_memory_pointer}};

			ir.CreateCall(
				internal_funcs.script_object_constructor->getFunctionType(),
				internal_funcs.script_object_constructor,
				scriptobject_constructor_args);

			// Constructor
			asCScriptFunction& constructor = *static_cast<asCScriptEngine&>(engine).scriptFunctions[constructor_id];

			llvm::Value* target_pointer = load_stack_value(
				m_stack_pointer - constructor.GetSpaceNeededForArguments(), defs.pvoid->getPointerTo());

			// TODO: check if target_pointer is null before the store (we really should)
			ir.CreateStore(object_memory_pointer, target_pointer);

			m_stack_pointer -= AS_PTR_SIZE;
			store_stack_value(m_stack_pointer, object_memory_pointer);

			emit_script_call(constructor);
		}
		else
		{
			asllvm_assert(false && "unsupported object category for script allocation");
		}
	}

	case asBC_FREE:
	{
		m_compiler.diagnostic("STUB: not freeing user object!!", asMSGTYPE_WARNING);
		break;
	}

	case asBC_LOADOBJ:
	{
		llvm::Value* pointer_to_object = load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.pvoid);
		ir.CreateStore(pointer_to_object, m_object_register);
		store_stack_value(
			asBC_SWORDARG0(instruction.pointer), ir.CreatePtrToInt(llvm::ConstantInt::get(defs.iptr, 0), defs.pvoid));

		break;
	}

	case asBC_STOREOBJ:
	{
		store_stack_value(asBC_SWORDARG0(instruction.pointer), m_object_register);
		ir.CreateStore(ir.CreatePtrToInt(llvm::ConstantInt::get(defs.iptr, 0), defs.pvoid), m_object_register);
		break;
	}

	case asBC_GETOBJ: unimpl(); break;
	case asBC_REFCPY: unimpl(); break;
	case asBC_CHKREF: unimpl(); break;
	case asBC_GETOBJREF: unimpl(); break;
	case asBC_GETREF:
	{
		llvm::Value* pointer = get_stack_value_pointer(m_stack_pointer - asBC_WORDARG0(instruction.pointer), defs.pi32);

		llvm::Value*                      index = ir.CreateLoad(defs.i32, ir.CreateBitCast(pointer, defs.pi32));
		const std::array<llvm::Value*, 2> indices{{llvm::ConstantInt::get(defs.i64, 0), index}};
		llvm::Value*                      variable_address = ir.CreateGEP(m_locals, indices);

		ir.CreateStore(variable_address, ir.CreateBitCast(pointer, defs.pi32->getPointerTo()));

		break;
	}
	case asBC_PshNull: unimpl(); break;
	case asBC_ClrVPtr: unimpl(); break;
	case asBC_OBJTYPE: unimpl(); break;
	case asBC_TYPEID: unimpl(); break;

	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:
	{
		store_stack_value(
			asBC_SWORDARG0(instruction.pointer), llvm::ConstantInt::get(defs.i32, asBC_DWORDARG(instruction.pointer)));

		break;
	}

	case asBC_SetV8:
	{
		store_stack_value(
			asBC_SWORDARG0(instruction.pointer), llvm::ConstantInt::get(defs.i64, asBC_QWORDARG(instruction.pointer)));

		break;
	}

	case asBC_ADDSi: unimpl(); break;

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
		store_value_register_value(load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32));
		break;
	}

	case asBC_CpyVtoR8:
	{
		store_value_register_value(load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i64));
		break;
	}

	case asBC_CpyVtoG4:
	{
		llvm::Value* global_ptr
			= ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, asBC_PTRARG(instruction.pointer)), defs.pi32);
		llvm::Value* value = load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i32);
		ir.CreateStore(value, global_ptr);
		break;
	}

	case asBC_CpyRtoV4:
	{
		store_stack_value(asBC_SWORDARG0(instruction.pointer), load_value_register_value(defs.i32));
		break;
	}

	case asBC_CpyRtoV8:
	{
		store_stack_value(asBC_SWORDARG0(instruction.pointer), load_value_register_value(defs.i64));
		break;
	}

	case asBC_CpyGtoV4:
	{
		llvm::Value* global_ptr
			= ir.CreateIntToPtr(llvm::ConstantInt::get(defs.iptr, asBC_PTRARG(instruction.pointer)), defs.pi32);
		store_stack_value(asBC_SWORDARG0(instruction.pointer), ir.CreateLoad(global_ptr, defs.i32));
		break;
	}

	case asBC_WRTV1: unimpl(); break;
	case asBC_WRTV2: unimpl(); break;
	case asBC_WRTV4: unimpl(); break;
	case asBC_WRTV8: unimpl(); break;
	case asBC_RDR1: unimpl(); break;
	case asBC_RDR2: unimpl(); break;
	case asBC_RDR4: unimpl(); break;
	case asBC_RDR8: unimpl(); break;
	case asBC_LDG: unimpl(); break;
	case asBC_LDV: unimpl(); break;

	case asBC_PGA:
	{
		m_stack_pointer += AS_PTR_SIZE;
		store_stack_value(m_stack_pointer, llvm::ConstantInt::get(defs.i64, asBC_PTRARG(instruction.pointer)));
		break;
	}

	case asBC_CmpPtr: unimpl(); break;

	case asBC_VAR:
	{
		m_stack_pointer += AS_PTR_SIZE;
		store_stack_value(m_stack_pointer, llvm::ConstantInt::get(defs.i64, asBC_SWORDARG0(instruction.pointer)));
		break;
	}

	case asBC_iTOf: unimpl(); break;
	case asBC_fTOi: unimpl(); break;
	case asBC_uTOf: unimpl(); break;
	case asBC_fTOu: unimpl(); break;

	case asBC_sbTOi: emit_stack_integer_sign_extend(instruction, defs.i8, defs.i32); break;
	case asBC_swTOi: emit_stack_integer_sign_extend(instruction, defs.i16, defs.i32); break;
	case asBC_ubTOi: emit_stack_integer_zero_extend(instruction, defs.i8, defs.i32); break;
	case asBC_uwTOi: emit_stack_integer_zero_extend(instruction, defs.i16, defs.i32); break;

	case asBC_dTOi: unimpl(); break;
	case asBC_dTOu: unimpl(); break;
	case asBC_dTOf: unimpl(); break;

	case asBC_iTOd: unimpl(); break;
	case asBC_uTOd: unimpl(); break;
	case asBC_fTOd: unimpl(); break;

	case asBC_ADDi:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Add, defs.i32);
		break;
	}

	case asBC_SUBi:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Sub, defs.i32);
		break;
	}

	case asBC_MULi:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Mul, defs.i32);
		break;
	}

	case asBC_DIVi:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::SDiv, defs.i32);
		break;
	}

	case asBC_MODi:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::SRem, defs.i32);
		break;
	}

	case asBC_ADDf: unimpl(); break;
	case asBC_SUBf: unimpl(); break;
	case asBC_MULf: unimpl(); break;
	case asBC_MODf: unimpl(); break;

	case asBC_ADDd: unimpl(); break;
	case asBC_SUBd: unimpl(); break;
	case asBC_MULd: unimpl(); break;
	case asBC_MODd: unimpl(); break;

	case asBC_ADDIi: unimpl(); break;

	case asBC_SUBIi:
	{
		emit_stack_arithmetic_imm(instruction, llvm::Instruction::Sub, defs.i32);
		break;
	}

	case asBC_MULIi: unimpl(); break;

	case asBC_ADDIf: unimpl(); break;
	case asBC_SUBIf: unimpl(); break;
	case asBC_MULIf: unimpl(); break;

	case asBC_SetG4: unimpl(); break;

	case asBC_ChkRefS: unimpl(); break;
	case asBC_ChkNullV: unimpl(); break;

	case asBC_CALLINTF: unimpl(); break;

	case asBC_iTOb: emit_stack_integer_trunc(instruction, defs.i32, defs.i8); break;
	case asBC_iTOw:
		emit_stack_integer_trunc(instruction, defs.i32, defs.i16);
		break;

		// SetV1/V2 grouped with V4 further up

	case asBC_Cast: unimpl(); break;

	case asBC_i64TOi: emit_stack_integer_trunc(instruction, defs.i64, defs.i32); break;
	case asBC_uTOi64: emit_stack_integer_zero_extend(instruction, defs.i32, defs.i64); break;
	case asBC_iTOi64: emit_stack_integer_sign_extend(instruction, defs.i32, defs.i64); break;

	case asBC_fTOi64: unimpl(); break;
	case asBC_dTOi64: unimpl(); break;
	case asBC_fTOu64: unimpl(); break;
	case asBC_dTOu64: unimpl(); break;

	case asBC_i64TOf: unimpl(); break;
	case asBC_u64TOf: unimpl(); break;
	case asBC_i64TOd: unimpl(); break;
	case asBC_u64TOd: unimpl(); break;

	case asBC_NEGi64: unimpl(); break;
	case asBC_INCi64: unimpl(); break;
	case asBC_DECi64: unimpl(); break;
	case asBC_BNOT64: unimpl(); break;

	case asBC_ADDi64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Add, defs.i64);
		break;
	}

	case asBC_SUBi64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Sub, defs.i64);
		break;
	}

	case asBC_MULi64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Mul, defs.i64);
		break;
	}

	case asBC_DIVi64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::SDiv, defs.i64);
		break;
	}

	case asBC_MODi64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::SRem, defs.i64);
		break;
	}

	case asBC_BAND64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::And, defs.i64);
		break;
	}

	case asBC_BOR64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Or, defs.i64);
		break;
	}

	case asBC_BXOR64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Xor, defs.i64);
		break;
	}

	case asBC_BSLL64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::Shl, defs.i64);
		break;
	}

	case asBC_BSRL64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::LShr, defs.i64);
		break;
	}

	case asBC_BSRA64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::AShr, defs.i64);
		break;
	}

	case asBC_CMPi64: unimpl(); break;
	case asBC_CMPu64: unimpl(); break;
	case asBC_ChkNullS: unimpl(); break;
	case asBC_ClrHi: unimpl(); break;

	case asBC_JitEntry:
	{
		if (m_compiler.config().verbose)
		{
			m_compiler.diagnostic("Found JIT entry point, patching as valid entry point");
		}

		// Pass the JitCompiler as the jitArg value, which can be used by lazy_jit_compiler().
		// TODO: this is probably UB
		asBC_PTRARG(instruction.pointer) = reinterpret_cast<asPWORD>(&m_compiler);

		break;
	}

	case asBC_CallPtr: unimpl(); break;
	case asBC_FuncPtr: unimpl(); break;
	case asBC_LoadThisR: unimpl(); break;

	case asBC_PshV8:
	{
		m_stack_pointer += 2;
		store_stack_value(m_stack_pointer, load_stack_value(asBC_SWORDARG0(instruction.pointer), defs.i64));
		break;
	}

	case asBC_DIVu:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::UDiv, defs.i32);
		break;
	}

	case asBC_MODu:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::URem, defs.i32);
		break;
	}

	case asBC_DIVu64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::UDiv, defs.i64);
		break;
	}

	case asBC_MODu64:
	{
		emit_stack_arithmetic(instruction, llvm::Instruction::URem, defs.i64);
		break;
	}

	case asBC_LoadRObjR: unimpl(); break;
	case asBC_LoadVObjR: unimpl(); break;
	case asBC_RefCpyV: unimpl(); break;
	case asBC_JLowZ: unimpl(); break;
	case asBC_JLowNZ: unimpl(); break;
	case asBC_AllocMem: unimpl(); break;
	case asBC_SetListSize: unimpl(); break;
	case asBC_PshListElmnt: unimpl(); break;
	case asBC_SetListType: unimpl(); break;
	case asBC_POWi: unimpl(); break;
	case asBC_POWu: unimpl(); break;
	case asBC_POWd: unimpl(); break;
	case asBC_POWdi: unimpl(); break;
	case asBC_POWi64: unimpl(); break;
	case asBC_POWu64: unimpl(); break;
	case asBC_Thiscall1: unimpl(); break;

	default:
	{
		asllvm_assert(false && "unrecognized instruction - are you using an unsupported AS version?");
	}
	}

	if (const auto expected_increment = instruction.info->stackInc; expected_increment != 0xFFFF)
	{
		asllvm_assert(m_stack_pointer - old_stack_pointer == expected_increment);
	}
}

std::string FunctionBuilder::disassemble(FunctionBuilder::InstructionContext instruction)
{
	// Handle certain instructions specifically
	switch (instruction.info->bc)
	{
	case asBC_JitEntry:
	case asBC_SUSPEND:
	{
		return {};
	}

	case asBC_CALLSYS:
	{
		auto* func
			= static_cast<asCScriptFunction*>(m_compiler.engine().GetFunctionById(asBC_INTARG(instruction.pointer)));

		asllvm_assert(func != nullptr);

		return fmt::format("CALLSYS {} # {}", func->GetName(), func->GetDeclaration(true, true, true));
	}

	default: break;
	}

	// Disassemble based on the generic instruction type
	// TODO: figure out variable references
	switch (instruction.info->type)
	{
	case asBCTYPE_NO_ARG:
	{
		return {};
	}

	case asBCTYPE_W_ARG:
	case asBCTYPE_wW_ARG:
	case asBCTYPE_rW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, asBC_SWORDARG0(instruction.pointer));
	}

	case asBCTYPE_DW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, asBC_INTARG(instruction.pointer));
	}

	case asBCTYPE_rW_DW_ARG:
	case asBCTYPE_wW_DW_ARG:
	case asBCTYPE_W_DW_ARG:
	{
		return fmt::format(
			"{} {} {}", instruction.info->name, asBC_SWORDARG0(instruction.pointer), asBC_INTARG(instruction.pointer));
	}

	case asBCTYPE_QW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, asBC_PTRARG(instruction.pointer));
	}

	case asBCTYPE_DW_DW_ARG:
	{
		// TODO: double check this
		return fmt::format(
			"{} {} {}", instruction.info->name, asBC_INTARG(instruction.pointer), asBC_INTARG(instruction.pointer + 1));
	}

	case asBCTYPE_wW_rW_rW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			asBC_SWORDARG0(instruction.pointer),
			asBC_SWORDARG1(instruction.pointer),
			asBC_SWORDARG2(instruction.pointer));
	}

	case asBCTYPE_wW_QW_ARG:
	{
		return fmt::format(
			"{} {} {}",
			instruction.info->name,
			asBC_SWORDARG0(instruction.pointer),
			asBC_PTRARG(instruction.pointer + 1));
	}

	case asBCTYPE_wW_rW_ARG:
	case asBCTYPE_rW_rW_ARG:
	case asBCTYPE_wW_W_ARG:
	{
		return fmt::format(
			"{} {} {}",
			instruction.info->name,
			asBC_SWORDARG0(instruction.pointer),
			asBC_SWORDARG1(instruction.pointer));
	}

	case asBCTYPE_wW_rW_DW_ARG:
	case asBCTYPE_rW_W_DW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			asBC_SWORDARG0(instruction.pointer),
			asBC_SWORDARG1(instruction.pointer),
			asBC_INTARG(instruction.pointer + 1));
	}

	case asBCTYPE_QW_DW_ARG:
	{
		return fmt::format(
			"{} {} {}", instruction.info->name, asBC_PTRARG(instruction.pointer), asBC_INTARG(instruction.pointer + 2));
	}

	case asBCTYPE_rW_QW_ARG:
	{
		return fmt::format(
			"{} {} {}",
			instruction.info->name,
			asBC_SWORDARG0(instruction.pointer),
			asBC_PTRARG(instruction.pointer + 1));
	}

	case asBCTYPE_rW_DW_DW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			asBC_SWORDARG0(instruction.pointer),
			asBC_INTARG(instruction.pointer + 1),
			asBC_INTARG(instruction.pointer + 2));
	}

	default:
	{
		return fmt::format("(unimplemented)");
	}
	}
}

void FunctionBuilder::emit_allocate_local_structures()
{
	llvm::IRBuilder<>& ir          = m_compiler.builder().ir();
	CommonDefinitions& defs        = m_compiler.builder().definitions();
	auto&              script_data = *m_script_function.scriptData;

	// TODO: use scriptData instead of having redundant fields for this
	m_locals_size          = script_data.variableSpace;
	m_max_extra_stack_size = script_data.stackNeeded - m_locals_size;

	m_locals          = ir.CreateAlloca(llvm::ArrayType::get(defs.i32, script_data.stackNeeded), 0, "locals");
	m_value_register  = ir.CreateAlloca(defs.i64, 0, "valuereg");
	m_object_register = ir.CreateAlloca(defs.pvoid, 0, "objreg");

	m_stack_pointer = m_locals_size;
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

	llvm::Value* original  = load_stack_value(stack_offset, source);
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

	llvm::Value* original  = load_stack_value(stack_offset, source);
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

	store_value_register_value(cmp);
}

void FunctionBuilder::emit_system_call(asCScriptFunction& function)
{
	llvm::IRBuilder<>&          ir   = m_compiler.builder().ir();
	CommonDefinitions&          defs = m_compiler.builder().definitions();
	asSSystemFunctionInterface& intf = *function.sysFuncIntf;

	llvm::Function* callee = m_module_builder.get_system_function(function);

	const std::size_t         argument_count = callee->arg_size();
	std::vector<llvm::Value*> args(argument_count);

	// TODO: should use references were possible, *const for consistency for now though
	llvm::Type* const return_type    = callee->getReturnType();
	llvm::Value*      return_pointer = nullptr;

	if (function.DoesReturnOnStack() && !intf.hostReturnInMemory)
	{
		return_pointer = load_stack_value(m_stack_pointer, return_type->getPointerTo());
		m_stack_pointer -= AS_PTR_SIZE;
	}

	const auto pop_sret_pointer = [&] {
		llvm::Argument* llvm_argument = &*(callee->arg_begin() + 0);
		llvm::Value*    value         = load_stack_value(m_stack_pointer, llvm_argument->getType());
		m_stack_pointer -= AS_PTR_SIZE;
		return value;
	};

	for (std::size_t i = 0, insert_index = 0, script_param_index = 0; i < argument_count; ++i)
	{
		const auto pop_param = [&] {
			llvm::Argument* llvm_argument = &*(callee->arg_begin() + i);

			args[insert_index] = load_stack_value(m_stack_pointer, llvm_argument->getType());
			m_stack_pointer -= function.parameterTypes[script_param_index].GetSizeOnStackDWords();

			++insert_index;
			++script_param_index;
		};

		switch (intf.callConv)
		{
		case ICC_THISCALL:
		case ICC_CDECL_OBJFIRST:
		{
			if (intf.hostReturnInMemory)
			{
				// 'this' pointer
				if (i == 0)
				{
					llvm::Argument* llvm_argument = &*(callee->arg_begin() + 1);
					args[1]                       = load_stack_value(m_stack_pointer, llvm_argument->getType());
					m_stack_pointer -= AS_PTR_SIZE;
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
					llvm::Argument* llvm_argument = &*(callee->arg_begin() + 0);
					args[0]                       = load_stack_value(m_stack_pointer, llvm_argument->getType());
					m_stack_pointer -= AS_PTR_SIZE;
					insert_index = 1;
					break;
				}
			}

			pop_param();

			break;
		}

		case ICC_CDECL_OBJLAST:
		{
			if (intf.hostReturnInMemory)
			{
				// 'this' pointer
				if (i == 0)
				{
					llvm::Argument* llvm_argument = &*(callee->arg_begin() + 1);
					args.back()                   = load_stack_value(m_stack_pointer, llvm_argument->getType());
					m_stack_pointer -= AS_PTR_SIZE;
					break;
				}

				if (i == 1)
				{
					args[0]      = pop_sret_pointer();
					insert_index = 1;
					break;
				}
			}
			else
			{
				// 'this' pointer
				if (i == 0)
				{
					llvm::Argument* llvm_argument = &*(callee->arg_begin() + 0);
					args.back()                   = load_stack_value(m_stack_pointer, llvm_argument->getType());
					m_stack_pointer -= AS_PTR_SIZE;
					break;
				}
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

	llvm::Value* result = ir.CreateCall(callee->getFunctionType(), callee, args);

	if (return_pointer == nullptr)
	{
		if (return_type != defs.tvoid)
		{
			store_value_register_value(result);
		}
	}
	else
	{
		ir.CreateStore(result, return_pointer);
	}
}

void FunctionBuilder::emit_script_call(asCScriptFunction& function)
{
	llvm::IRBuilder<>& ir   = m_compiler.builder().ir();
	CommonDefinitions& defs = m_compiler.builder().definitions();

	llvm::Value*                new_frame_pointer = get_stack_value_pointer(m_stack_pointer, defs.i32);
	std::array<llvm::Value*, 1> args{{new_frame_pointer}};

	llvm::Function* callee = m_module_builder.create_function(function);

	llvm::CallInst* ret = ir.CreateCall(callee->getFunctionType(), callee, args);

	if (function.returnType.GetTokenType() != ttVoid)
	{
		if (function.DoesReturnOnStack())
		{
			m_stack_pointer -= function.GetSpaceNeededForReturnValue();
		}
		else
		{
			// Store to the value register
			llvm::Value* typed_value_register = ir.CreateBitCast(m_value_register, ret->getType()->getPointerTo());
			ir.CreateStore(ret, typed_value_register);
		}
	}

	m_stack_pointer -= function.GetSpaceNeededForArguments();
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
	if (i <= 0)
	{
		std::array<llvm::Value*, 1> indices{{llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), -i)}};

		return ir.CreateGEP(&*(m_llvm_function->arg_begin() + 0), indices);
	}

	// Get a pointer to that value on the local stack
	{
		const long local_offset = m_locals_size + m_max_extra_stack_size - i;

		// Ensure offset is within alloca boundaries
		asllvm_assert(local_offset >= 0 && local_offset < m_locals_size + m_max_extra_stack_size);

		std::array<llvm::Value*, 2> indices{
			{llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), 0),
			 llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(context), local_offset)}};

		return ir.CreateGEP(m_locals, indices);
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
	return m_jump_map.at(instruction.offset + 2 + asBC_INTARG(instruction.pointer));
}

llvm::BasicBlock* FunctionBuilder::get_conditional_fail_branch_target(FunctionBuilder::InstructionContext instruction)
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
} // namespace asllvm::detail
