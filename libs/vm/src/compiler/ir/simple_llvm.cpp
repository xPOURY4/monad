#include <compiler/compiler.h>
#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/ir/simple_llvm.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <format>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::bytecode;

    llvm::FunctionType *contract_entrypoint_type()
    {
        auto *void_ty = llvm::Type::getVoidTy(context());
        auto *ptr_ty = llvm::PointerType::getUnqual(context());

        return llvm::FunctionType::get(void_ty, {ptr_ty}, false);
    }

    llvm::Function *build_entrypoint(llvm::Module &mod)
    {
        return llvm::Function::Create(
            contract_entrypoint_type(),
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            constants::contract_entrypoint,
            mod);
    }

    /**
     * Compile a slow dynamic jump table function based on the program's
     * JUMPDEST instructions.
     *
     * This function takes a single argument representing the byte offset into
     * the original program that should be jumped to, and returns a block
     * address constant that can be indirectly branched to. If the argument
     * offset was not a JUMPDEST instruction in the original program, then the
     * returned block address is null.
     */
    llvm::Function *build_slow_jump_table(
        llvm::Module &mod,
        std::unordered_map<byte_offset, block_id> const &jump_table,
        std::vector<std::pair<llvm::BasicBlock *, Block>> const &blocks)
    {
        auto *block_address_ty = llvm::PointerType::getUnqual(context());
        auto *block_id_ty = llvm::IntegerType::get(context(), 256);

        auto *table_fn_ty =
            llvm::FunctionType::get(block_address_ty, {block_id_ty}, false);

        auto *table_fn = llvm::Function::Create(
            table_fn_ty,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            constants::jump_table,
            mod);

        auto *entry = llvm::BasicBlock::Create(context(), "entry", table_fn);
        auto *error = llvm::BasicBlock::Create(context(), "error", table_fn);
        auto *exit = llvm::BasicBlock::Create(context(), "exit", table_fn);

        auto b = llvm::IRBuilder(error);
        b.CreateRet(llvm::ConstantPointerNull::get(block_address_ty));

        b.SetInsertPoint(exit);
        auto *phi = b.CreatePHI(block_address_ty, 0);
        b.CreateRet(phi);

        b.SetInsertPoint(entry);

        auto *offset_arg = table_fn->getArg(0);
        auto *switch_table = b.CreateSwitch(offset_arg, error);

        for (auto const &[offset, block_id] : jump_table) {
            assert(block_id < blocks.size() && "Invalid block ID");

            auto *case_block = llvm::BasicBlock::Create(
                context(), std::format("jumpdest_{}", offset), table_fn);
            b.SetInsertPoint(case_block);
            b.CreateBr(exit);

            auto *offset_const = llvm::ConstantInt::get(block_id_ty, offset);
            switch_table->addCase(offset_const, case_block);

            auto [llvm_block, _] = blocks[block_id];
            auto *block_addr = llvm::BlockAddress::get(llvm_block);
            phi->addIncoming(block_addr, case_block);
        }

        return table_fn;
    }

    llvm::IntegerType *word_type()
    {
        return llvm::IntegerType::get(context(), 256);
    }

    llvm::IntegerType *stack_pointer_type()
    {
        return llvm::IntegerType::get(context(), 16);
    }

    llvm::GlobalVariable *build_evm_stack(llvm::Module &mod)
    {
        constexpr auto stack_size = 1024;

        auto *stack_type = llvm::ArrayType::get(word_type(), stack_size);

        auto *stack = new llvm::GlobalVariable(
            stack_type,
            false,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            llvm::ConstantAggregateZero::get(stack_type),
            constants::stack);

        mod.insertGlobalVariable(stack);
        return stack;
    }

    llvm::GlobalVariable *build_evm_stack_pointer(llvm::Module &mod)
    {
        auto *stack_ptr = new llvm::GlobalVariable(
            stack_pointer_type(),
            false,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            llvm::ConstantInt::get(stack_pointer_type(), 0),
            constants::stack_pointer);

        mod.insertGlobalVariable(stack_ptr);
        return stack_ptr;
    }

    llvm::GlobalVariable *build_evm_gas_left(llvm::Module &mod)
    {
        auto *i64_ty = llvm::IntegerType::get(context(), 64);

        auto *gas_left = new llvm::GlobalVariable(
            i64_ty,
            false,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            nullptr,
            constants::gas_left);

        mod.insertGlobalVariable(gas_left);
        return gas_left;
    }
}

namespace monad::compiler
{
    SimpleLLVMIR::SimpleLLVMIR(BasicBlocksIR const &instrs)
        : mod(std::make_unique<llvm::Module>("monad-evm", context()))
        , entry_point(build_entrypoint(*mod))
        , stack(build_evm_stack(*mod))
        , stack_pointer(build_evm_stack_pointer(*mod))
        , gas_left(build_evm_gas_left(*mod))
        , push(build_push_function())
        , pop(build_pop_function())
        , entry(llvm::BasicBlock::Create(context(), "entry", entry_point))
        , stop(llvm::BasicBlock::Create(context(), "stop", entry_point))
        , revert(llvm::BasicBlock::Create(context(), "revert", entry_point))
        , self_destruct(
              llvm::BasicBlock::Create(context(), "self_destruct", entry_point))
        , return_(llvm::BasicBlock::Create(context(), "return", entry_point))
    {
        assert(
            instrs.blocks.size() > 0 &&
            "Cannot compile program with no basic blocks");

        for (auto const &b : instrs.blocks) {
            evm_blocks.emplace_back(compile_block(b), b);
        }

        auto b = llvm::IRBuilder(entry);
        b.CreateBr(evm_blocks[0].first);

        for (auto *exn_block : {revert, self_destruct, return_}) {
            b.SetInsertPoint(exn_block);
            b.CreateRetVoid();
        }

        jump_table = build_slow_jump_table(*mod, instrs.jumpdests, evm_blocks);

        auto *host_interface = entry_point->getArg(0);

        b.SetInsertPoint(stop);
        b.Insert(call_stop(host_interface));
        b.CreateRetVoid();

        compile_block_terminators();
    }

    compile_result SimpleLLVMIR::result() &&
    {
        return {std::move(mod), entry_point};
    }

    llvm::Function *SimpleLLVMIR::build_push_function()
    {
        auto *fn_ty = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context()), {word_type()}, false);

        auto *push_fn = llvm::Function::Create(
            fn_ty,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            constants::push,
            *mod);

        auto *entry = llvm::BasicBlock::Create(context(), "entry", push_fn);
        auto b = llvm::IRBuilder(entry);

        auto *zero =
            llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(context()), 0);
        auto *sp_ptr = b.CreateGEP(stack_pointer_type(), stack_pointer, {zero});

        auto *sp = b.CreateLoad(stack_pointer_type(), sp_ptr);

        auto *stack_item = b.CreateGEP(word_type(), stack, {sp});
        b.CreateStore(push_fn->getArg(0), stack_item);

        auto *one = llvm::ConstantInt::get(stack_pointer_type(), 1);
        auto *new_sp = b.CreateAdd(sp, one);
        b.CreateStore(new_sp, sp_ptr);

        b.CreateRetVoid();
        return push_fn;
    }

    llvm::Function *SimpleLLVMIR::build_pop_function()
    {
        auto *fn_ty = llvm::FunctionType::get(word_type(), {}, false);

        auto *pop_fn = llvm::Function::Create(
            fn_ty,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            constants::pop,
            *mod);

        auto *entry = llvm::BasicBlock::Create(context(), "entry", pop_fn);
        auto b = llvm::IRBuilder(entry);

        auto *zero =
            llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(context()), 0);
        auto *sp_ptr = b.CreateGEP(stack_pointer_type(), stack_pointer, {zero});

        auto *sp = b.CreateLoad(stack_pointer_type(), sp_ptr);

        auto *one = llvm::ConstantInt::get(stack_pointer_type(), 1);
        auto *new_sp = b.CreateSub(sp, one);
        b.CreateStore(new_sp, sp_ptr);

        auto *stack_item = b.CreateGEP(word_type(), stack, {new_sp});
        auto *ret_val = b.CreateLoad(word_type(), stack_item);
        b.CreateRet(ret_val);

        return pop_fn;
    }

    llvm::BasicBlock *SimpleLLVMIR::compile_block(Block const &b) const
    {
        auto *block = llvm::BasicBlock::Create(context(), "evm", entry_point);
        auto builder = llvm::IRBuilder(block);
        for (auto const &inst : b.instrs) {
            compile_instruction(builder, inst);
        }
        return block;
    }

    void SimpleLLVMIR::compile_block_terminators()
    {
        auto b = llvm::IRBuilder(context());
        for (auto const &[llvm_block, original_block] : evm_blocks) {
            b.SetInsertPoint(llvm_block);

            switch (original_block.terminator) {
            case Terminator::Jump: {
                auto *offset = b.Insert(call_pop());
                auto *dest = b.Insert(call_jump_table(offset));
                b.Insert(dynamic_jump(dest));
                break;
            }

            case Terminator::JumpI: {
                auto *offset = b.Insert(call_pop());
                auto *cond_word = b.Insert(call_pop());

                auto *fallthrough_cond =
                    b.CreateICmpEQ(cond_word, b.getIntN(256, 0));

                auto *dynamic_br_block = llvm::BasicBlock::Create(
                    context(), "", llvm_block->getParent());

                b.CreateCondBr(
                    fallthrough_cond,
                    evm_blocks[original_block.fallthrough_dest].first,
                    dynamic_br_block);

                b.SetInsertPoint(dynamic_br_block);
                auto *dest = b.Insert(call_jump_table(offset));
                b.Insert(dynamic_jump(dest));
                break;
            }

            case Terminator::JumpDest:
                llvm::BranchInst::Create(
                    evm_blocks[original_block.fallthrough_dest].first,
                    llvm_block);
                break;

            case Terminator::Stop:
                b.CreateBr(stop);
                break;

            case Terminator::Revert:
                b.CreateBr(revert);
                break;

            case Terminator::SelfDestruct:
                b.CreateBr(self_destruct);
                break;

            case Terminator::Return:
                b.CreateBr(return_);
                break;
            }
        }
    }

    void SimpleLLVMIR::compile_instruction(
        llvm::IRBuilder<> &b, Instruction const &inst) const
    {
        auto op = inst.opcode;
        if (op >= PUSH0 && op <= PUSH32) {
            b.Insert(call_push(inst.data));

            auto gas = (op == PUSH0) ? 2 : 3;
            spend_static_gas(b, gas);
        }
    }

    void SimpleLLVMIR::spend_static_gas(llvm::IRBuilder<> &b, int64_t gas) const
    {
        auto *global = mod->getGlobalVariable(constants::gas_left);
        auto *old_val = b.CreateLoad(b.getInt64Ty(), global);
        auto *new_val = b.CreateSub(old_val, b.getInt64(gas));
        b.CreateStore(new_val, global);
    }

    llvm::CallInst *SimpleLLVMIR::call_pop() const
    {
        return llvm::CallInst::Create(pop->getFunctionType(), pop);
    }

    llvm::CallInst *SimpleLLVMIR::call_push(uint256_t immediate) const
    {
        auto words = std::array<uint64_t, 4>{};
        for (auto i = 0u; i < 4; ++i) {
            words[i] = immediate[i];
        }

        auto big_val = llvm::APInt(256, words);
        auto *arg = llvm::ConstantInt::get(context(), big_val);

        return llvm::CallInst::Create(push->getFunctionType(), push, {arg});
    }

    llvm::CallInst *SimpleLLVMIR::call_jump_table(llvm::Value *arg) const
    {
        return llvm::CallInst::Create(
            jump_table->getFunctionType(), jump_table, {arg});
    }

    llvm::CallInst *SimpleLLVMIR::call_stop(llvm::Value *interface) const
    {
        auto *ptr_ty = llvm::PointerType::getUnqual(context());
        auto *stop_ty = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context()), {ptr_ty}, false);

        auto fn = mod->getOrInsertFunction("monad_evm_runtime_stop", stop_ty);
        return llvm::CallInst::Create(fn, {interface});
    }

    llvm::IndirectBrInst *SimpleLLVMIR::dynamic_jump(llvm::Value *dest) const
    {
        auto *inst = llvm::IndirectBrInst::Create(dest, 0);
        for (auto const &[llvm_block, _] : evm_blocks) {
            inst->addDestination(llvm_block);
        }
        return inst;
    }
}
