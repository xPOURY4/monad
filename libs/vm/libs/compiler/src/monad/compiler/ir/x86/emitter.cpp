#include <monad/compiler/ir/basic_blocks.hpp>
#include <monad/compiler/ir/x86.hpp>
#include <monad/compiler/ir/x86/emitter.hpp>
#include <monad/compiler/ir/x86/virtual_stack.hpp>
#include <monad/compiler/types.hpp>
#include <monad/runtime/math.hpp>
#include <monad/runtime/types.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/uint256.hpp>

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <asmjit/core/api-config.h>
#include <asmjit/core/codeholder.h>
#include <asmjit/core/emitter.h>
#include <asmjit/core/globals.h>
#include <asmjit/core/jitruntime.h>
#include <asmjit/core/operand.h>
#include <asmjit/x86/x86operand.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace runtime = monad::runtime;
namespace x86 = asmjit::x86;

using monad::utils::Cases;

static_assert(ASMJIT_ARCH_X86 == 64);

constexpr auto reg_context = x86::rbx;
constexpr auto reg_stack = x86::rbp;

constexpr auto context_offset_gas_remaining =
    offsetof(runtime::Context, gas_remaining);
constexpr auto context_offset_exit_stack_ptr =
    offsetof(runtime::Context, exit_stack_ptr);
constexpr auto context_offset_env_recipient =
    offsetof(runtime::Context, env) + offsetof(runtime::Environment, recipient);
constexpr auto context_offset_env_sender =
    offsetof(runtime::Context, env) + offsetof(runtime::Environment, sender);
constexpr auto context_offset_env_value =
    offsetof(runtime::Context, env) + offsetof(runtime::Environment, value);
constexpr auto context_offset_env_input_data_size =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, input_data_size);
constexpr auto context_offset_env_return_data_size =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, return_data_size);
constexpr auto context_offset_env_tx_context_origin =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, tx_origin);
constexpr auto context_offset_env_tx_context_tx_gas_price =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, tx_gas_price);
constexpr auto context_offset_env_tx_context_block_gas_limit =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, block_gas_limit);
constexpr auto context_offset_env_tx_context_block_coinbase =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, block_coinbase);
constexpr auto context_offset_env_tx_context_block_timestamp =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, block_timestamp);
constexpr auto context_offset_env_tx_context_block_number =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, block_number);
constexpr auto context_offset_env_tx_context_block_prev_randao =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, block_prev_randao);
constexpr auto context_offset_env_tx_context_chain_id =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, chain_id);
constexpr auto context_offset_env_tx_context_block_base_fee =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, block_base_fee);
constexpr auto context_offset_env_tx_context_blob_base_fee =
    offsetof(runtime::Context, env) +
    offsetof(runtime::Environment, tx_context) +
    offsetof(evmc_tx_context, blob_base_fee);
constexpr auto context_offset_memory_size =
    offsetof(runtime::Context, memory) + offsetof(runtime::Memory, size);
constexpr auto context_offset_result_offset =
    offsetof(runtime::Context, result) + offsetof(runtime::Result, offset);
constexpr auto context_offset_result_size =
    offsetof(runtime::Context, result) + offsetof(runtime::Result, size);
constexpr auto context_offset_result_status =
    offsetof(runtime::Context, result) + offsetof(runtime::Result, status);

constexpr auto sp_offset_arg1 = 0;
constexpr auto sp_offset_arg2 = sp_offset_arg1 + 8;
constexpr auto sp_offset_arg3 = sp_offset_arg2 + 8;
constexpr auto sp_offset_arg4 = sp_offset_arg3 + 8;
constexpr auto sp_offset_arg5 = sp_offset_arg4 + 8;
constexpr auto sp_offset_arg6 = sp_offset_arg5 + 8;
constexpr auto sp_offset_stack_size = sp_offset_arg6 + 8;

constexpr auto stack_frame_size = sp_offset_stack_size + 8;

namespace
{
    using namespace monad::compiler::native;

    bool is_uint64_bounded(uint64_t x)
    {
        int64_t const i = std::bit_cast<int64_t>(x);
        constexpr int64_t upper =
            static_cast<int64_t>(std::numeric_limits<int32_t>::max());
        constexpr int64_t lower =
            static_cast<int64_t>(std::numeric_limits<int32_t>::min());
        return i <= upper && i >= lower;
    }

    bool is_literal_bounded(Literal lit)
    {
        for (size_t i = 0; i < 4; ++i) {
            if (!is_uint64_bounded(lit.value[i])) {
                return false;
            }
        }
        return true;
    }

    Emitter::Imm256 literal_to_imm256(Literal lit)
    {
        return {
            asmjit::Imm{static_cast<int32_t>(lit.value[0])},
            asmjit::Imm{static_cast<int32_t>(lit.value[1])},
            asmjit::Imm{static_cast<int32_t>(lit.value[2])},
            asmjit::Imm{static_cast<int32_t>(lit.value[3])}};
    }

    x86::Mem stack_offset_to_mem(StackOffset offset)
    {
        return x86::qword_ptr(x86::rbp, offset.offset * 32);
    }

    x86::Ymm avx_reg_to_ymm(AvxReg reg)
    {
        MONAD_COMPILER_DEBUG_ASSERT(reg.reg < 32);
        return x86::Ymm(reg.reg);
    }

    void runtime_print_gas_remaining_impl(
        char const *msg, runtime::Context const *ctx)
    {
        std::cout << msg << ": gas remaining: " << ctx->gas_remaining
                  << std::endl;
    }

    void runtime_print_input_stack_impl(
        char const *msg, monad::utils::uint256_t *stack, uint64_t stack_size)
    {
        std::cout << msg << ": stack: ";
        for (size_t i = 0; i < stack_size; ++i) {
            std::cout << '(' << i << ": " << intx::to_string(stack[-i - 1])
                      << ')';
        }
        std::cout << std::endl;
    }

    void runtime_print_top2_impl(
        char const *msg, monad::utils::uint256_t const *x,
        monad::utils::uint256_t const *y)
    {
        std::cout << msg << ": " << intx::to_string(*x) << " and "
                  << intx::to_string(*y) << std::endl;
    }
}

// For reducing noise
#define GENERAL_BIN_INSTR(i0, i1)                                              \
    general_bin_instr<                                                         \
        {&x86::Assembler::i0,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1},                                                 \
        {&x86::Assembler::i0,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1},                                                 \
        {&x86::Assembler::i0,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1},                                                 \
        {&x86::Assembler::i0,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1},                                                 \
        {&x86::Assembler::i0,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1,                                                  \
         &x86::Assembler::i1}>

// For reducing noise
#define AVX_OR_GENERAL_BIN_INSTR(i, v)                                         \
    avx_or_general_bin_instr<                                                  \
        {&x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i},                                                  \
        {&x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i},                                                  \
        {&x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i},                                                  \
        {&x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i},                                                  \
        {&x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i,                                                   \
         &x86::Assembler::i},                                                  \
        &x86::Assembler::v,                                                    \
        &x86::Assembler::v>

namespace monad::compiler::native
{
    Emitter::Error::Error(std::string const &msg)
        : std::runtime_error{msg}
    {
    }

    Emitter::Error::Error(char const *msg)
        : std::runtime_error{msg}
    {
    }

    void Emitter::EmitErrorHandler::handleError(
        asmjit::Error, char const *msg, asmjit::BaseEmitter *)
    {
        throw Emitter::Error(std::format("x86 emitter error: {}", msg));
    }

    char const *Emitter::location_type_to_string(LocationType loc)
    {
        switch (loc) {
        case Emitter::LocationType::AvxReg:
            return "AvxReg";
        case Emitter::LocationType::GeneralReg:
            return "GeneralReg";
        case Emitter::LocationType::StackOffset:
            return "StackOffset";
        case Emitter::LocationType::Literal:
            return "Literal";
        default:
            MONAD_COMPILER_ASSERT(false);
        }
    }

    Emitter::RuntimeImpl &Emitter::RuntimeImpl::pass(StackElemRef &&elem)
    {
        if (!elem->stack_offset() && !elem->literal()) {
            em_->mov_stack_elem_to_stack_offset(elem);
        }
        explicit_args_.push_back(std::move(elem));
        return *this;
    }

    void Emitter::RuntimeImpl::call_impl()
    {
        MONAD_COMPILER_ASSERT(
            explicit_args_.size() + implicit_arg_count() == arg_count_);
        MONAD_COMPILER_DEBUG_ASSERT(arg_count_ <= MAX_RUNTIME_ARGS);
        MONAD_COMPILER_DEBUG_ASSERT(
            !context_arg_.has_value() || context_arg_ != result_arg_);
        MONAD_COMPILER_DEBUG_ASSERT(
            !context_arg_.has_value() || context_arg_ != remaining_gas_arg_);
        MONAD_COMPILER_DEBUG_ASSERT(
            !result_arg_.has_value() || result_arg_ != remaining_gas_arg_);

        for (size_t a = 0, i = 0; i < arg_count_; ++i) {
            auto u = std::optional{i};
            if (u == context_arg_ || u == result_arg_ ||
                u == remaining_gas_arg_) {
                continue;
            }
            StackElemRef const elem = explicit_args_[a++];
            if (elem->stack_offset()) {
                mov_arg(i, stack_offset_to_mem(*elem->stack_offset()));
            }
            else {
                MONAD_COMPILER_DEBUG_ASSERT(elem->literal().has_value());
                auto lbl = em_->append_literal(*elem->literal());
                mov_arg(i, x86::qword_ptr(lbl));
            }
        }

        // Clear stack elements to deallocate registers and stack offsets:
        explicit_args_.clear();

        if (context_arg_.has_value()) {
            mov_arg(*context_arg_, reg_context);
        }
        if (remaining_gas_arg_.has_value()) {
            mov_arg(*remaining_gas_arg_, remaining_base_gas_);
        }
        if (result_arg_.has_value()) {
            auto result =
                em_->stack_.alloc_stack_offset(em_->stack_.top_index() + 1);
            mov_arg(*result_arg_, stack_offset_to_mem(*result->stack_offset()));
            em_->stack_.push(std::move(result));
        }

        if (spill_avx_) {
            em_->as_.vzeroupper();
        }
        auto lbl = em_->append_external_function(runtime_fun_);
        em_->as_.call(x86::qword_ptr(lbl));
    }

    size_t Emitter::RuntimeImpl::implicit_arg_count()
    {
        return context_arg_.has_value() + result_arg_.has_value() +
               remaining_gas_arg_.has_value();
    }

    size_t Emitter::RuntimeImpl::explicit_arg_count()
    {
        MONAD_COMPILER_DEBUG_ASSERT(arg_count_ >= implicit_arg_count());
        return arg_count_ - implicit_arg_count();
    }

    bool Emitter::RuntimeImpl::spill_avx_regs()
    {
        return spill_avx_;
    }

    void Emitter::RuntimeImpl::mov_arg(size_t arg_index, RuntimeArg &&arg)
    {
        static_assert(MAX_RUNTIME_ARGS == 12);
        switch (arg_index) {
        case 0:
            mov_reg_arg(x86::rdi, std::move(arg));
            break;
        case 1:
            mov_reg_arg(x86::rsi, std::move(arg));
            break;
        case 2:
            mov_reg_arg(x86::rdx, std::move(arg));
            break;
        case 3:
            mov_reg_arg(x86::rcx, std::move(arg));
            break;
        case 4:
            mov_reg_arg(x86::r8, std::move(arg));
            break;
        case 5:
            mov_reg_arg(x86::r9, std::move(arg));
            break;
        case 6:
            mov_stack_arg(sp_offset_arg1, std::move(arg));
            break;
        case 7:
            mov_stack_arg(sp_offset_arg2, std::move(arg));
            break;
        case 8:
            mov_stack_arg(sp_offset_arg3, std::move(arg));
            break;
        case 9:
            mov_stack_arg(sp_offset_arg4, std::move(arg));
            break;
        case 10:
            mov_stack_arg(sp_offset_arg5, std::move(arg));
            break;
        case 11:
            mov_stack_arg(sp_offset_arg6, std::move(arg));
            break;
        default:
            MONAD_COMPILER_ASSERT(false);
        }
    }

    void Emitter::RuntimeImpl::mov_reg_arg(
        asmjit::x86::Gpq const &reg, RuntimeArg &&arg)
    {
        std::visit(
            Cases{
                [&](x86::Gpq const &x) { em_->as_.mov(reg, x); },
                [&](asmjit::Imm const &x) { em_->as_.mov(reg, x); },
                [&](x86::Mem const &x) { em_->as_.lea(reg, x); },
            },
            arg);
    }

    void
    Emitter::RuntimeImpl::mov_stack_arg(int32_t sp_offset, RuntimeArg &&arg)
    {
        std::visit(
            Cases{
                [&](x86::Gpq const &x) {
                    em_->as_.mov(x86::qword_ptr(x86::rsp, sp_offset), x);
                },
                [&](asmjit::Imm const &x) {
                    em_->as_.mov(x86::qword_ptr(x86::rsp, sp_offset), x);
                },
                [&](x86::Mem const &x) {
                    em_->as_.lea(x86::rax, x);
                    em_->as_.mov(x86::qword_ptr(x86::rsp, sp_offset), x86::rax);
                },
            },
            arg);
    }

    Emitter::Emitter(
        asmjit::JitRuntime const &rt, uint64_t codesize, char const *log_path)
        : as_{init_code_holder(rt, log_path)}
        , epilogue_label_{as_.newNamedLabel("ContractEpilogue")}
        , error_label_{as_.newNamedLabel("Error")}
        , jump_table_label_{as_.newNamedLabel("JumpTable")}
        , keep_stack_in_next_block_{}
        , gpq256_regs_{Gpq256{x86::r12, x86::r13, x86::r14, x86::r15}, Gpq256{x86::r8, x86::r9, x86::r10, x86::r11}, Gpq256{x86::rcx, x86::rsi, x86::rdx, x86::rdi}}
        , rcx_general_reg{2}
        , rdx_general_reg{2}
        , rcx_general_reg_index{}
        , rdx_general_reg_index{2}
        , bytecode_size_{codesize}
    {
        contract_prologue();
    }

    Emitter::~Emitter()
    {
        if (debug_logger_.file()) {
            int const err = fclose(debug_logger_.file());
            MONAD_COMPILER_ASSERT(err == 0);
        }
    }

    entrypoint_t Emitter::finish_contract(asmjit::JitRuntime &rt)
    {
        contract_epilogue();

        for (auto const &[lbl, rpq, back] : byte_out_of_bounds_handlers_) {
            as_.align(asmjit::AlignMode::kCode, 16);
            as_.bind(lbl);
            as_.xor_(rpq[0], rpq[0]);
            as_.xor_(rpq[1], rpq[1]);
            as_.xor_(rpq[2], rpq[2]);
            as_.xor_(rpq[3], rpq[3]);
            as_.jmp(back);
        }

        error_block(error_label_, runtime::StatusCode::Error);

        static char const *const ro_section_name = "ro";
        static auto const ro_section_name_len = 2;
        static auto const ro_section_index = 1;

        // Inside asmjit, if a section is emitted with no actual data in it, a
        // call to memcpy with a null source is made. This is technically UB,
        // and will get flagged by ubsan as such, even if it is technically
        // harmless in practice. The only way that we can emit an empty section
        // is if the compiled contract is completely empty: if there are any
        // code bytes at all, the ro section will have some data in it and the
        // UB won't occur.
        if (bytecode_size_ > 0) {
            asmjit::Section *ro_section;
            code_holder_.newSection(
                &ro_section,
                ro_section_name,
                ro_section_name_len,
                asmjit::SectionFlags::kReadOnly,
                32,
                ro_section_index);

            as_.section(ro_section);
            as_.align(asmjit::AlignMode::kData, 32);
            for (auto [lbl, lit] : literals_) {
                as_.bind(lbl);
                as_.embed(&lit.value[0], 32);
            }

            for (auto [lbl, f] : external_functions_) {
                as_.bind(lbl);
                static_assert(sizeof(void *) == sizeof(uint64_t));
                as_.embedUInt64(reinterpret_cast<uint64_t>(f));
            }

            // We are 8 byte aligned.
            as_.bind(jump_table_label_);
            for (size_t bid = 0; bid < bytecode_size_; ++bid) {
                auto lbl = jump_dests_.find(bid);
                if (lbl != jump_dests_.end()) {
                    as_.embedLabel(lbl->second);
                    // as_.embedLabelDelta(lbl->second, jump_table_label_, 4);
                }
                else {
                    as_.embedLabel(error_label_);
                    // as_.embedLabelDelta(bad_jumpdest_label_,
                    // jump_table_label_, 4);
                }
            }

            for (auto const &[lbl, msg] : debug_messages_) {
                as_.bind(lbl);
                as_.embed(msg.c_str(), msg.size() + 1);
            }
        }

        entrypoint_t contract_main;
        auto err = rt.add(&contract_main, &code_holder_);
        if (err != asmjit::kErrorOk) {
            throw Error{asmjit::DebugUtils::errorAsString(err)};
        }
        return contract_main;
    }

    asmjit::CodeHolder *Emitter::init_code_holder(
        asmjit::JitRuntime const &rt, char const *log_path)
    {
        code_holder_.setErrorHandler(&error_handler_);
        if (log_path) {
            FILE *log_file = fopen(log_path, "w");
            MONAD_COMPILER_ASSERT(log_file);
            debug_logger_.setFile(log_file);
            code_holder_.setLogger(&debug_logger_);
        }
        code_holder_.init(rt.environment(), rt.cpuFeatures());
        return &code_holder_;
    }

    void Emitter::contract_prologue()
    {
        // Arguments
        // rdi: context pointer
        // rsi: stack pointer

        as_.push(x86::rbp); // 16 byte aligned
        as_.push(x86::rbx); // unaligned
        as_.push(x86::r12); // 16 byte aligned
        as_.push(x86::r13); // unaligned
        as_.push(x86::r14); // 16 byte aligned
        as_.push(x86::r15); // unaligned

        as_.mov(reg_context, x86::rdi);
        as_.mov(reg_stack, x86::rsi);
        as_.mov(x86::ptr(reg_context, context_offset_exit_stack_ptr), x86::rsp);

        static_assert(stack_frame_size % 16 == 8);
        as_.sub(x86::rsp, stack_frame_size); // 16 byte aligned

        as_.mov(x86::qword_ptr(x86::rsp, sp_offset_stack_size), 0);
    }

    void Emitter::contract_epilogue()
    {
        as_.align(asmjit::AlignMode::kCode, 16);
        as_.bind(epilogue_label_);
        as_.vzeroupper();
        as_.add(x86::rsp, stack_frame_size);
        as_.pop(x86::r15);
        as_.pop(x86::r14);
        as_.pop(x86::r13);
        as_.pop(x86::r12);
        as_.pop(x86::rbx);
        as_.pop(x86::rbp);
        as_.ret();
    }

    bool Emitter::is_debug_enabled()
    {
        return debug_logger_.file() != nullptr;
    }

    void Emitter::runtime_print_gas_remaining(std::string const &msg)
    {
        auto msg_lbl = as_.newLabel();
        debug_messages_.emplace_back(msg_lbl, msg);
        auto fn_lbl = as_.newLabel();
        external_functions_.emplace_back(
            fn_lbl, reinterpret_cast<void *>(runtime_print_gas_remaining_impl));

        discharge_deferred_comparison();
        spill_caller_save_regs(true);
        as_.lea(x86::rdi, x86::qword_ptr(msg_lbl));
        as_.mov(x86::rsi, reg_context);
        as_.vzeroupper();
        as_.call(x86::qword_ptr(fn_lbl));
    }

    void Emitter::runtime_print_input_stack(std::string const &msg)
    {
        auto msg_lbl = as_.newLabel();
        debug_messages_.emplace_back(msg_lbl, msg);
        auto fn_lbl = as_.newLabel();
        external_functions_.emplace_back(
            fn_lbl, reinterpret_cast<void *>(runtime_print_input_stack_impl));

        discharge_deferred_comparison();
        spill_caller_save_regs(true);
        as_.lea(x86::rdi, x86::qword_ptr(msg_lbl));
        as_.mov(x86::rsi, reg_stack);
        as_.mov(x86::rdx, x86::qword_ptr(x86::rsp, sp_offset_stack_size));
        as_.vzeroupper();
        as_.call(x86::qword_ptr(fn_lbl));
    }

    void Emitter::runtime_print_top2(std::string const &msg)
    {
        auto msg_lbl = as_.newLabel();
        debug_messages_.emplace_back(msg_lbl, msg);
        auto fn_lbl = as_.newLabel();
        external_functions_.emplace_back(
            fn_lbl, reinterpret_cast<void *>(runtime_print_top2_impl));

        discharge_deferred_comparison();
        spill_caller_save_regs(true);

        as_.lea(x86::rdi, x86::qword_ptr(msg_lbl));

        auto e1 = stack_.get(stack_.top_index());
        if (!e1->stack_offset() && !e1->literal()) {
            mov_stack_elem_to_stack_offset(e1);
        }
        if (e1->stack_offset().has_value()) {
            as_.lea(x86::rsi, stack_offset_to_mem(e1->stack_offset().value()));
        }
        else {
            auto lit = append_literal(e1->literal().value());
            as_.lea(x86::rsi, x86::qword_ptr(lit));
        }
        auto e2 = stack_.get(stack_.top_index() - 1);
        if (!e2->stack_offset() && !e2->literal()) {
            mov_stack_elem_to_stack_offset(e2);
        }
        if (e2->stack_offset().has_value()) {
            as_.lea(x86::rdx, stack_offset_to_mem(e2->stack_offset().value()));
        }
        else {
            auto lit = append_literal(e2->literal().value());
            as_.lea(x86::rdx, x86::qword_ptr(lit));
        }
        as_.vzeroupper();
        as_.call(x86::qword_ptr(fn_lbl));
    }

    void Emitter::breakpoint()
    {
        as_.int3();
    }

    Stack &Emitter::get_stack()
    {
        return stack_;
    }

    void Emitter::add_jump_dest(byte_offset d)
    {
        char name[2 * sizeof(byte_offset) + 2];
        static_assert(sizeof(byte_offset) <= sizeof(long));
        auto isize = snprintf(name, sizeof(name), "B%lx", d);
        auto size = static_cast<size_t>(isize);
        MONAD_COMPILER_DEBUG_ASSERT(size < sizeof(name));
        jump_dests_.emplace(d, as_.newNamedLabel(name, size));
    }

    bool Emitter::begin_new_block(basic_blocks::Block const &b)
    {
        if (is_debug_enabled()) {
            debug_comment(std::format("{}", b));
        }
        if (keep_stack_in_next_block_) {
            stack_.continue_block(b);
        }
        else {
            stack_.begin_new_block(b);
        }
        return block_prologue(b);
    }

    void Emitter::gas_decrement_no_check(int32_t gas)
    {
        as_.sub(x86::qword_ptr(reg_context, context_offset_gas_remaining), gas);
    }

    void Emitter::gas_decrement_check_non_negative(int32_t gas)
    {
        gas_decrement_no_check(gas);
        as_.jl(error_label_);
    }

    void Emitter::spill_caller_save_regs(bool spill_avx)
    {
        // Spill general regs first, because if stack element is in both
        // general register and avx register then stack element will be
        // moved to stack using avx register.
        spill_all_caller_save_general_regs();
        if (spill_avx) {
            spill_all_avx_regs();
        }
    }

    void Emitter::spill_all_caller_save_general_regs()
    {
        for (auto const &[reg, off] :
             stack_.spill_all_caller_save_general_regs()) {
            Gpq256 const &gpq = general_reg_to_gpq256(reg);
            x86::Mem m = stack_offset_to_mem(off);
            for (size_t i = 0; i < 4; ++i) {
                as_.mov(m, gpq[i]);
                m.addOffset(8);
            }
        }
    }

    void Emitter::spill_all_avx_regs()
    {
        for (auto const &[reg, off] : stack_.spill_all_avx_regs()) {
            as_.vmovaps(stack_offset_to_mem(off), avx_reg_to_ymm(reg));
        }
    }

    std::pair<StackElemRef, AvxRegReserv> Emitter::alloc_avx_reg()
    {
        auto [elem, reserv, offset] = stack_.alloc_avx_reg();
        if (offset.has_value()) {
            as_.vmovaps(
                stack_offset_to_mem(*offset), avx_reg_to_ymm(*elem->avx_reg()));
        }
        return {elem, reserv};
    }

    AvxRegReserv Emitter::insert_avx_reg(StackElemRef elem)
    {
        auto [reserv, offset] = stack_.insert_avx_reg(elem);
        if (offset.has_value()) {
            as_.vmovaps(
                stack_offset_to_mem(*offset), avx_reg_to_ymm(*elem->avx_reg()));
        }
        return reserv;
    }

    std::pair<StackElemRef, GeneralRegReserv> Emitter::alloc_general_reg()
    {
        auto [elem, reserv, offset] = stack_.alloc_general_reg();
        if (offset.has_value()) {
            mov_general_reg_to_mem(
                *elem->general_reg(), stack_offset_to_mem(*offset));
        }
        return {elem, reserv};
    }

    GeneralRegReserv Emitter::insert_general_reg(StackElemRef elem)
    {
        auto [reserv, offset] = stack_.insert_general_reg(elem);
        if (offset.has_value()) {
            mov_general_reg_to_mem(
                *elem->general_reg(), stack_offset_to_mem(*offset));
        }
        return reserv;
    }

    template <typename... LiveSet, size_t... Is>
    bool Emitter::is_live(
        StackElemRef elem, std::tuple<LiveSet...> const &live,
        std::index_sequence<Is...>)
    {
        return elem->is_on_stack() || (... || (elem == std::get<Is>(live)));
    }

    template <typename... LiveSet>
    bool Emitter::is_live(StackElemRef elem, std::tuple<LiveSet...> const &live)
    {
        return is_live(
            std::move(elem), live, std::index_sequence_for<LiveSet...>{});
    }

    template <typename... LiveSet, size_t... Is>
    bool Emitter::is_live(
        GeneralReg reg, std::tuple<LiveSet...> const &live,
        std::index_sequence<Is...>)
    {
        return stack_.is_general_reg_on_stack(reg) ||
               (... ||
                (std::optional{reg} == std::get<Is>(live)->general_reg()));
    }

    template <typename... LiveSet>
    bool Emitter::is_live(GeneralReg reg, std::tuple<LiveSet...> const &live)
    {
        return is_live(reg, live, std::index_sequence_for<LiveSet...>{});
    }

    bool Emitter::block_prologue(basic_blocks::Block const &b)
    {
        bool const keep_stack = keep_stack_in_next_block_;
        keep_stack_in_next_block_ = false;

        auto it = jump_dests_.find(b.offset);
        if (it != jump_dests_.end()) {
            as_.bind(it->second);
        }

        if (!keep_stack && is_debug_enabled()) {
            runtime_print_gas_remaining(
                std::format("Block 0x{:02x}", b.offset));
        }

        auto const min_delta = stack_.min_delta();
        auto const max_delta = stack_.max_delta();
        if (min_delta < -1024 || max_delta > 1024) {
            as_.jmp(error_label_);
            return false;
        }
        auto const size_mem = x86::qword_ptr(x86::rsp, sp_offset_stack_size);
        if (stack_.did_min_delta_decrease()) {
            as_.cmp(size_mem, -min_delta);
            as_.jb(error_label_);
        }
        if (stack_.did_max_delta_increase()) {
            as_.cmp(size_mem, 1024 - max_delta);
            as_.ja(error_label_);
        }
        return true;
    }

    template <bool preserve_eflags>
    void Emitter::adjust_by_stack_delta()
    {
        auto const delta = stack_.delta();
        if (delta != 0) {
            auto ssm = x86::qword_ptr(x86::rsp, sp_offset_stack_size);
            if constexpr (preserve_eflags) {
                as_.mov(x86::rax, ssm);
                as_.lea(x86::rax, x86::ptr(x86::rax, delta));
                as_.lea(x86::rbp, x86::ptr(x86::rbp, delta * 32));
                as_.mov(ssm, x86::rax);
            }
            else {
                as_.add(ssm, delta);
                as_.add(x86::rbp, delta * 32);
            }
        }
    }

    // Does not update eflags
    void Emitter::write_to_final_stack_offsets()
    {
        // Write stack elements to their final stack offsets before
        // leaving basic block. If stack element `e` is currently at
        // stack indices `0`, `1` and only located in an AVX register,
        // then we need to move the AVX register to both stack offsets
        // `0` and `1`.

        MONAD_COMPILER_ASSERT(!stack_.has_deferred_comparison());

        int32_t const top_index = stack_.top_index();
        int32_t const min_delta = stack_.min_delta();
        if (top_index < min_delta) {
            // Nothing on the stack.
            return;
        }

        // Reserve an AVX register which we will use for temporary values
        auto [init1, init1_reserv] = alloc_avx_reg();
        auto init_yx1 = avx_reg_to_ymm(*init1->avx_reg());
        auto yx1 = init_yx1;

        // Definition. Stack element `e` depends on stack element `d` if
        //   * `d` is located on some stack offset `i` and
        //   * `i` is element of `e.stack_indices()` and
        //   * `d` is not located in AVX register and
        //   * `e != d`.
        //
        // Such a dependency means that `d` is occupying a final stack offset
        // to which stack element `e` needs to be located before leaving the
        // basic block. The below map `dep_counts` is used to count the number
        // of dependencies of all the stack elements on the stack.
        std::unordered_map<StackElem *, int32_t> dep_counts;
        for (int32_t i = min_delta; i <= top_index; ++i) {
            auto d = stack_.get(i);

            MONAD_COMPILER_DEBUG_ASSERT(
                d->general_reg().has_value() || d->avx_reg().has_value() ||
                d->stack_offset().has_value() || d->literal().has_value());

            if (i != *d->stack_indices().begin()) {
                // Already visited
                continue;
            }
            dep_counts.insert({d.get(), 0}); // No override
            if (!d->stack_offset().has_value()) {
                continue;
            }
            int32_t const offset = d->stack_offset()->offset;
            if (offset > top_index) {
                continue;
            }
            auto *e = stack_.get(offset).get();
            if (e == d.get()) {
                continue;
            }
            if (d->avx_reg().has_value()) {
                continue;
            }
            ++dep_counts[e];
        }

        // The `non_dep` vector contains all the stack elements without
        // dependencies.
        std::vector<StackElem *> non_dep;
        for (auto const &[e, c] : dep_counts) {
            if (c == 0) {
                non_dep.push_back(e);
            }
        }

        // Write all the stack elements without dependencies. Suppose stack
        // element `e` depends on stack element `d` and `d` does not have
        // any dependencies, i.e. is element of `non_dep`. After writing `d`
        // to its final stack offsets, we decrease `dep_counts[e]`, because
        // it is now safe to write `e` to the stack offset which was occupied
        // by `d`. Insert `e` into `non_dep` if `dep_counts[e]` becomes zero.
        while (!non_dep.empty()) {
            StackElem *d = non_dep.back();
            non_dep.pop_back();
            auto const &is = d->stack_indices();
            MONAD_COMPILER_DEBUG_ASSERT(is.size() >= 1);
            auto it = is.begin();
            if (is.size() == 1 && d->stack_offset().has_value() &&
                d->stack_offset()->offset == *it) {
                // Stack element d is already located on the final stack offset.
                continue;
            }
            if (!d->avx_reg()) {
                // Put stack element d in the `yx1` AVX register.
                if (d->stack_offset()) {
                    as_.vmovaps(yx1, stack_offset_to_mem(*d->stack_offset()));
                }
                else if (d->literal()) {
                    mov_literal_to_ymm(*d->literal(), yx1);
                }
                else {
                    MONAD_COMPILER_DEBUG_ASSERT(d->general_reg().has_value());
                    auto m = stack_offset_to_mem(StackOffset{*it});
                    // Move to final stack offset:
                    mov_general_reg_to_mem(*d->general_reg(), m);
                    // Point to next stack offset:
                    ++it;
                    // Put in `yx1` if there are more final stack offsets:
                    if (it != is.end()) {
                        as_.vmovaps(yx1, m);
                    }
                }
            }
            else {
                // Stack element d is already located in an AVX register,
                // which we can use.
                yx1 = avx_reg_to_ymm(*d->avx_reg());
            }
            // Move to remaining final stack offsets:
            for (; it != is.end(); ++it) {
                if (!d->stack_offset() || d->stack_offset()->offset != *it) {
                    as_.vmovaps(stack_offset_to_mem(StackOffset{*it}), yx1);
                }
            }
            // Decrease dependency count of the stack element which depends on
            // `d`, if such stack element exists.
            if (!d->avx_reg() && d->stack_offset()) {
                int32_t const i = d->stack_offset()->offset;
                if (i > stack_.top_index()) {
                    continue;
                }
                StackElem *e = stack_.get(i).get();
                if (e == d) {
                    continue;
                }
                MONAD_COMPILER_DEBUG_ASSERT(dep_counts[e] > 0);
                if (--dep_counts[e] == 0) {
                    non_dep.push_back(e);
                }
            }
        }

        // We are not necessarily done, because there may remain cycles of
        // stack elements. E.g. stack element `e` depends on stack
        // element `d` and `d` depends on `e`. In this case, `e` and `d`
        // still have dependency count 1. It is not possible for a stack
        // element to have dependency count more than 1 at this point.

        // Later we will need two available AVX registers `yx2` and `yx1`.
        auto yx2 = yx1;
        // If there is a free avx register, then we can use it for `yx2`.
        // Otherwise we have necessarily updated `yx1` in the prior loop,
        // so the current value of `yx1` will work for `yx2`.
        if (stack_.has_free_avx_reg()) {
            auto [y, _, spill] = stack_.alloc_avx_reg();
            MONAD_COMPILER_DEBUG_ASSERT(!spill);
            yx2 = avx_reg_to_ymm(*y->avx_reg());
        }
        yx1 = init_yx1;
        MONAD_COMPILER_DEBUG_ASSERT(yx1 != yx2);

        // Write the remaining stack elements in cycles to their final stack
        // offsets.
        for (auto const [e, ec] : dep_counts) {
            MONAD_COMPILER_DEBUG_ASSERT(ec >= 0);
            if (ec == 0) {
                // Since stack element e as no dependencies, it has
                // already been written it its final stack offsets.
                continue;
            }

            std::vector<StackElem *> cycle;
            cycle.reserve(2);
            StackElem *d = e;
            do {
                MONAD_COMPILER_DEBUG_ASSERT(dep_counts[d] == 1);
                MONAD_COMPILER_DEBUG_ASSERT(!d->avx_reg().has_value());
                MONAD_COMPILER_DEBUG_ASSERT(d->stack_offset().has_value());
                dep_counts[d] = 0;
                cycle.push_back(d);
                MONAD_COMPILER_DEBUG_ASSERT(
                    d->stack_offset()->offset <= stack_.top_index());
                d = stack_.get(d->stack_offset()->offset).get();
            }
            while (d != e);

            MONAD_COMPILER_DEBUG_ASSERT(cycle.size() >= 2);
            as_.vmovaps(
                yx1, stack_offset_to_mem(*cycle.back()->stack_offset()));

            // Write all the stack elements in the cycle containing e to
            // their final stack offsets.
            for (size_t k = cycle.size(); k > 1;) {
                --k;
                // Invariant:
                // stack element `cycle[k]` is located in AVX register `yx1`.
                as_.vmovaps(
                    yx2, stack_offset_to_mem(*cycle[k - 1]->stack_offset()));
                for (int32_t const i : cycle[k]->stack_indices()) {
                    as_.vmovaps(stack_offset_to_mem(StackOffset{i}), yx1);
                }
                std::swap(yx1, yx2);
            }
            for (int32_t const i : e->stack_indices()) {
                as_.vmovaps(stack_offset_to_mem(StackOffset{i}), yx1);
            }
        }
    }

    void Emitter::discharge_deferred_comparison()
    {
        if (!stack_.has_deferred_comparison()) {
            return;
        }
        auto dc = stack_.discharge_deferred_comparison();
        if (dc.stack_elem) {
            discharge_deferred_comparison(dc.stack_elem, dc.comparison);
        }
        if (dc.negated_stack_elem) {
            auto comp = negate_comparison(dc.comparison);
            discharge_deferred_comparison(dc.negated_stack_elem, comp);
        }
    }

    void Emitter::debug_comment(std::string const &msg)
    {
        MONAD_COMPILER_ASSERT(is_debug_enabled());
        std::stringstream ss{msg};
        std::string line;
        while (std::getline(ss, line, '\n')) {
            debug_logger_.log("// ");
            debug_logger_.log(line.c_str());
            debug_logger_.log("\n");
        }
    }

    // Does not update eflags
    void
    Emitter::discharge_deferred_comparison(StackElem *elem, Comparison comp)
    {
        auto [temp_reg, reserv] = alloc_avx_reg();
        auto y = avx_reg_to_ymm(*temp_reg->avx_reg());
        auto m = stack_offset_to_mem(*elem->stack_offset());
        as_.vpxor(y, y, y);
        as_.vmovaps(m, y);
        switch (comp) {
        case Comparison::Below:
            as_.setb(m);
            break;
        case Comparison::AboveEqual:
            as_.setae(m);
            break;
        case Comparison::Above:
            as_.seta(m);
            break;
        case Comparison::BelowEqual:
            as_.setbe(m);
            break;
        case Comparison::Less:
            as_.setl(m);
            break;
        case Comparison::GreaterEqual:
            as_.setge(m);
            break;
        case Comparison::Greater:
            as_.setg(m);
            break;
        case Comparison::LessEqual:
            as_.setle(m);
            break;
        case Comparison::Equal:
            as_.sete(m);
            break;
        case Comparison::NotEqual:
            as_.setne(m);
            break;
        }
    }

    asmjit::Label const &Emitter::append_literal(Literal lit)
    {
        literals_.emplace_back(as_.newLabel(), lit);
        return literals_.back().first;
    }

    asmjit::Label const &Emitter::append_external_function(void *f)
    {
        external_functions_.emplace_back(as_.newLabel(), f);
        return external_functions_.back().first;
    }

    Emitter::Gpq256 &Emitter::general_reg_to_gpq256(GeneralReg reg)
    {
        MONAD_COMPILER_DEBUG_ASSERT(reg.reg <= 2);
        return gpq256_regs_[reg.reg];
    }

    void Emitter::mov_stack_index_to_avx_reg(int32_t stack_index)
    {
        mov_stack_elem_to_avx_reg(stack_.get(stack_index));
    }

    void Emitter::mov_stack_index_to_general_reg(int32_t stack_index)
    {
        mov_stack_elem_to_general_reg(stack_.get(stack_index));
    }

    void Emitter::mov_stack_index_to_stack_offset(int32_t stack_index)
    {
        mov_stack_elem_to_stack_offset(stack_.get(stack_index));
    }

    template <bool assume_aligned>
    void
    Emitter::mov_literal_to_mem(Literal const &lit, asmjit::x86::Mem const &mem)
    {
        auto elem = stack_.alloc_literal(lit);
        mov_literal_to_avx_reg(elem);
        auto reg = *elem->avx_reg();
        if constexpr (assume_aligned) {
            as_.vmovaps(mem, avx_reg_to_ymm(reg));
        }
        else {
            mov_avx_reg_to_unaligned_mem(reg, mem);
        }
    }

    void
    Emitter::mov_general_reg_to_mem(GeneralReg reg, asmjit::x86::Mem const &mem)
    {
        x86::Mem temp{mem};
        for (auto r : general_reg_to_gpq256(reg)) {
            as_.mov(temp, r);
            temp.addOffset(8);
        }
    }

    void Emitter::mov_literal_to_unaligned_mem(
        Literal const &lit, asmjit::x86::Mem const &mem)
    {
        mov_literal_to_mem<false>(lit, mem);
    }

    void Emitter::mov_avx_reg_to_unaligned_mem(
        AvxReg reg, asmjit::x86::Mem const &mem)
    {
        as_.vmovups(mem, avx_reg_to_ymm(reg));
    }

    void Emitter::mov_stack_offset_to_unaligned_mem(
        StackOffset offset, asmjit::x86::Mem const &mem)
    {
        auto [elem, reserv] = alloc_avx_reg();
        AvxReg const reg = *elem->avx_reg();
        as_.vmovaps(avx_reg_to_ymm(reg), stack_offset_to_mem(offset));
        mov_avx_reg_to_unaligned_mem(reg, mem);
    }

    void Emitter::mov_stack_elem_to_unaligned_mem(
        StackElemRef elem, asmjit::x86::Mem const &mem)
    {
        if (elem->avx_reg()) {
            mov_avx_reg_to_unaligned_mem(*elem->avx_reg(), mem);
        }
        else if (elem->general_reg()) {
            mov_general_reg_to_mem(*elem->general_reg(), mem);
        }
        else if (elem->literal()) {
            mov_literal_to_unaligned_mem(*elem->literal(), mem);
        }
        else {
            MONAD_COMPILER_ASSERT(elem->stack_offset().has_value());
            mov_stack_offset_to_unaligned_mem(*elem->stack_offset(), mem);
        }
    }

    void Emitter::mov_general_reg_to_gpq256(GeneralReg reg, Gpq256 const &gpq)
    {
        Gpq256 const &temp = general_reg_to_gpq256(reg);
        for (size_t i = 0; i < 4; ++i) {
            as_.mov(gpq[i], temp[i]);
        }
    }

    void Emitter::mov_literal_to_gpq256(Literal const &lit, Gpq256 const &gpq)
    {
        if (stack_.has_deferred_comparison()) {
            for (size_t i = 0; i < 4; ++i) {
                as_.mov(gpq[i], lit.value[i]);
            }
        }
        else {
            for (size_t i = 0; i < 4; ++i) {
                auto const &r = gpq[i];
                if (lit.value[i] == 0) {
                    as_.xor_(r, r);
                }
                else {
                    as_.mov(r, lit.value[i]);
                }
            }
        }
    }

    void
    Emitter::mov_stack_offset_to_gpq256(StackOffset offset, Gpq256 const &gpq)
    {
        x86::Mem temp = stack_offset_to_mem(offset);
        for (size_t i = 0; i < 4; ++i) {
            as_.mov(gpq[i], temp);
            temp.addOffset(8);
        }
    }

    void Emitter::mov_stack_elem_to_gpq256(StackElemRef elem, Gpq256 const &gpq)
    {
        if (elem->general_reg()) {
            mov_general_reg_to_gpq256(*elem->general_reg(), gpq);
        }
        else if (elem->literal()) {
            mov_literal_to_gpq256(*elem->literal(), gpq);
        }
        else if (elem->stack_offset()) {
            mov_stack_offset_to_gpq256(*elem->stack_offset(), gpq);
        }
        else {
            MONAD_COMPILER_ASSERT(elem->avx_reg().has_value());
            mov_stack_elem_to_stack_offset(elem);
            mov_stack_offset_to_gpq256(*elem->stack_offset(), gpq);
        }
    }

    void Emitter::mov_literal_to_ymm(Literal const &lit, x86::Ymm const &y)
    {
        if (lit.value == std::numeric_limits<uint256_t>::max()) {
            as_.vpcmpeqd(y, y, y);
        }
        else if (lit.value == 0) {
            as_.vpxor(y, y, y);
        }
        else {
            auto lbl = append_literal(lit);
            as_.vmovaps(y, x86::ptr(lbl));
        }
    }

    void Emitter::mov_stack_elem_to_avx_reg(StackElemRef elem)
    {
        if (elem->avx_reg()) {
            return;
        }
        if (elem->literal()) {
            mov_literal_to_avx_reg(std::move(elem));
        }
        else if (elem->stack_offset()) {
            mov_stack_offset_to_avx_reg(std::move(elem));
        }
        else {
            MONAD_COMPILER_ASSERT(elem->general_reg().has_value());
            mov_general_reg_to_avx_reg(std::move(elem));
        }
    }

    void
    Emitter::mov_stack_elem_to_avx_reg(StackElemRef elem, int32_t preferred)
    {
        if (elem->avx_reg()) {
            return;
        }
        if (elem->literal()) {
            mov_literal_to_avx_reg(std::move(elem));
        }
        else if (elem->stack_offset()) {
            mov_stack_offset_to_avx_reg(std::move(elem));
        }
        else {
            MONAD_COMPILER_ASSERT(elem->general_reg().has_value());
            mov_general_reg_to_avx_reg(std::move(elem), preferred);
        }
    }

    void Emitter::mov_stack_elem_to_general_reg(StackElemRef elem)
    {
        if (elem->general_reg()) {
            return;
        }
        if (elem->literal()) {
            mov_literal_to_general_reg(std::move(elem));
        }
        else if (elem->stack_offset()) {
            mov_stack_offset_to_general_reg(std::move(elem));
        }
        else {
            MONAD_COMPILER_ASSERT(elem->avx_reg().has_value());
            mov_avx_reg_to_general_reg(std::move(elem));
        }
    }

    void
    Emitter::mov_stack_elem_to_general_reg(StackElemRef elem, int32_t preferred)
    {
        if (elem->general_reg()) {
            return;
        }
        if (elem->literal()) {
            mov_literal_to_general_reg(std::move(elem));
        }
        else if (elem->stack_offset()) {
            mov_stack_offset_to_general_reg(std::move(elem));
        }
        else {
            MONAD_COMPILER_ASSERT(elem->avx_reg().has_value());
            mov_avx_reg_to_general_reg(std::move(elem), preferred);
        }
    }

    void Emitter::mov_stack_elem_to_stack_offset(StackElemRef elem)
    {
        if (elem->stack_offset()) {
            return;
        }
        if (elem->avx_reg()) {
            mov_avx_reg_to_stack_offset(std::move(elem));
        }
        else if (elem->general_reg()) {
            mov_general_reg_to_stack_offset(std::move(elem));
        }
        else {
            MONAD_COMPILER_ASSERT(elem->literal().has_value());
            mov_literal_to_stack_offset(std::move(elem));
        }
    }

    void Emitter::mov_stack_elem_to_stack_offset(
        StackElemRef elem, int32_t preferred_offset)
    {
        if (elem->stack_offset()) {
            return;
        }
        if (elem->avx_reg()) {
            mov_avx_reg_to_stack_offset(std::move(elem), preferred_offset);
        }
        else if (elem->general_reg()) {
            mov_general_reg_to_stack_offset(std::move(elem), preferred_offset);
        }
        else {
            MONAD_COMPILER_ASSERT(elem->literal().has_value());
            mov_literal_to_stack_offset(std::move(elem), preferred_offset);
        }
    }

    void Emitter::mov_general_reg_to_avx_reg(StackElemRef elem)
    {
        int32_t const preferred = elem->preferred_stack_offset();
        mov_general_reg_to_avx_reg(std::move(elem), preferred);
    }

    void
    Emitter::mov_general_reg_to_avx_reg(StackElemRef elem, int32_t preferred)
    {
        mov_general_reg_to_stack_offset(elem, preferred);
        mov_stack_offset_to_avx_reg(elem);
    }

    void Emitter::mov_literal_to_avx_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->literal().has_value());
        auto avx_reserv = insert_avx_reg(elem);
        mov_literal_to_ymm(*elem->literal(), avx_reg_to_ymm(*elem->avx_reg()));
    }

    void Emitter::mov_stack_offset_to_avx_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->stack_offset().has_value());
        insert_avx_reg(elem);
        as_.vmovaps(
            avx_reg_to_ymm(*elem->avx_reg()),
            stack_offset_to_mem(*elem->stack_offset()));
    }

    void Emitter::mov_avx_reg_to_stack_offset(StackElemRef elem)
    {
        int32_t const preferred = elem->preferred_stack_offset();
        mov_avx_reg_to_stack_offset(std::move(elem), preferred);
    }

    void
    Emitter::mov_avx_reg_to_stack_offset(StackElemRef elem, int32_t preferred)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->avx_reg().has_value());
        stack_.insert_stack_offset(elem, preferred);
        auto y = avx_reg_to_ymm(*elem->avx_reg());
        as_.vmovaps(stack_offset_to_mem(*elem->stack_offset()), y);
    }

    void Emitter::mov_general_reg_to_stack_offset(StackElemRef elem)
    {
        int32_t const preferred = elem->preferred_stack_offset();
        mov_general_reg_to_stack_offset(std::move(elem), preferred);
    }

    void Emitter::mov_general_reg_to_stack_offset(
        StackElemRef elem, int32_t preferred)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->general_reg().has_value());
        stack_.insert_stack_offset(elem, preferred);
        mov_general_reg_to_mem(
            *elem->general_reg(), stack_offset_to_mem(*elem->stack_offset()));
    }

    void Emitter::mov_literal_to_stack_offset(StackElemRef elem)
    {
        int32_t const preferred = elem->preferred_stack_offset();
        mov_literal_to_stack_offset(std::move(elem), preferred);
    }

    void
    Emitter::mov_literal_to_stack_offset(StackElemRef elem, int32_t preferred)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->literal().has_value());
        stack_.insert_stack_offset(elem, preferred);
        mov_literal_to_mem<true>(
            *elem->literal(), stack_offset_to_mem(*elem->stack_offset()));
    }

    void Emitter::mov_avx_reg_to_general_reg(StackElemRef elem)
    {
        int32_t const preferred = elem->preferred_stack_offset();
        mov_avx_reg_to_general_reg(std::move(elem), preferred);
    }

    void
    Emitter::mov_avx_reg_to_general_reg(StackElemRef elem, int32_t preferred)
    {
        mov_avx_reg_to_stack_offset(elem, preferred);
        mov_stack_offset_to_general_reg(elem);
    }

    void Emitter::mov_literal_to_general_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->literal().has_value());
        insert_general_reg(elem);
        mov_literal_to_gpq256(
            *elem->literal(), general_reg_to_gpq256(*elem->general_reg()));
    }

    void Emitter::mov_stack_offset_to_general_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->stack_offset().has_value());
        insert_general_reg(elem);
        mov_stack_offset_to_gpq256(
            *elem->stack_offset(), general_reg_to_gpq256(*elem->general_reg()));
    }

    StackElem *
    Emitter::revertible_mov_stack_offset_to_general_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->stack_offset().has_value());
        StackElem *spill_elem = stack_.has_free_general_reg()
                                    ? nullptr
                                    : stack_.spill_general_reg();

        auto reg_elem = [this] {
            auto [x, _, spill] = stack_.alloc_general_reg();
            MONAD_COMPILER_DEBUG_ASSERT(!spill.has_value());
            return x;
        };
        stack_.move_general_reg(*reg_elem(), *elem);

        if (spill_elem != nullptr) {
            MONAD_COMPILER_DEBUG_ASSERT(spill_elem->stack_offset().has_value());
            mov_general_reg_to_mem(
                *elem->general_reg(),
                stack_offset_to_mem(*spill_elem->stack_offset()));
        }
        mov_stack_offset_to_gpq256(
            *elem->stack_offset(), general_reg_to_gpq256(*elem->general_reg()));

        return spill_elem;
    }

    // No discharge
    void Emitter::push(uint256_t const &x)
    {
        stack_.push_literal(x);
    }

    // No discharge
    void Emitter::pop()
    {
        stack_.pop();
    }

    // No discharge
    void Emitter::dup(uint8_t dup_ix)
    {
        MONAD_COMPILER_ASSERT(dup_ix > 0);
        stack_.dup(stack_.top_index() + 1 - static_cast<int32_t>(dup_ix));
    }

    // No discharge
    void Emitter::swap(uint8_t swap_ix)
    {
        MONAD_COMPILER_ASSERT(swap_ix > 0);
        stack_.swap(stack_.top_index() - static_cast<int32_t>(swap_ix));
    }

    // Discharge through `lt` overload
    void Emitter::lt()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        lt(std::move(left), std::move(right));
    }

    // Discharge through `lt` overload
    void Emitter::gt()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        lt(std::move(right), std::move(left));
    }

    // Discharge through `slt` overload
    void Emitter::slt()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        slt(std::move(left), std::move(right));
    }

    // Discharge through `slt` overload
    void Emitter::sgt()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        slt(std::move(right), std::move(left));
    }

    void Emitter::sub()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(sub(std::move(left), std::move(right), {}));
    }

    // Discharge
    template <typename... LiveSet>
    StackElemRef Emitter::sub(
        StackElemRef pre_dst, StackElemRef pre_src,
        std::tuple<LiveSet...> const &live)
    {
        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                return stack_.alloc_literal({x - y});
            }
        }
        else if (pre_src->literal() && pre_src->literal()->value == 0) {
            return pre_dst;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<false>(
            std::move(pre_dst),
            stack_.top_index() + 1,
            std::move(pre_src),
            live);

        GENERAL_BIN_INSTR(sub, sbb)
        (dst, dst_loc, src, src_loc, [](size_t i, uint64_t x) {
            return i == 0 && x == 0;
        });

        return dst;
    }

    void Emitter::add()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(add(std::move(left), std::move(right), {}));
    }

    // Discharge
    template <typename... LiveSet>
    StackElemRef Emitter::add(
        StackElemRef pre_dst, StackElemRef pre_src,
        std::tuple<LiveSet...> const &live)
    {
        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                return stack_.alloc_literal({x + y});
            }
            else if (pre_dst->literal()->value == 0) {
                return pre_src;
            }
        }
        else if (pre_src->literal() && pre_src->literal()->value == 0) {
            return pre_dst;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<true>(
            std::move(pre_dst),
            stack_.top_index() + 1,
            std::move(pre_src),
            live);

        GENERAL_BIN_INSTR(add, adc)
        (dst, dst_loc, src, src_loc, [](size_t i, uint64_t x) {
            return i == 0 && x == 0;
        });

        return dst;
    }

    // Discharge
    void Emitter::byte()
    {
        auto ix = stack_.pop();
        auto src = stack_.pop();

        if (ix->literal() && src->literal()) {
            auto const &i = ix->literal()->value;
            auto const &x = src->literal()->value;
            push(monad::utils::byte(i, x));
            return;
        }

        RegReserv const ix_reserv{ix};
        RegReserv const src_reserv{src};

        discharge_deferred_comparison();

        if (!src->stack_offset()) {
            mov_stack_elem_to_stack_offset(src);
        }
        if (ix->literal()) {
            byte_literal_ix(ix->literal()->value, *src->stack_offset());
            return;
        }
        if (ix->general_reg()) {
            byte_general_reg_or_stack_offset_ix(
                std::move(ix), *src->stack_offset());
            return;
        }
        if (!ix->stack_offset()) {
            mov_avx_reg_to_stack_offset(ix);
        }
        byte_general_reg_or_stack_offset_ix(
            std::move(ix), *src->stack_offset());
    }

    // Discharge
    void Emitter::signextend()
    {
        auto ix = stack_.pop();
        auto src = stack_.pop();

        if (ix->literal() && src->literal()) {
            auto const &i = ix->literal()->value;
            auto const &x = src->literal()->value;
            push(monad::utils::signextend(i, x));
            return;
        }

        RegReserv const ix_reserv{ix};
        RegReserv const src_reserv{src};

        discharge_deferred_comparison();

        if (ix->literal()) {
            signextend_literal_ix(ix->literal()->value, src);
            return;
        }
        if (ix->general_reg()) {
            signextend_stack_elem_ix(std::move(ix), src, {});
            return;
        }
        if (!ix->stack_offset()) {
            mov_avx_reg_to_stack_offset(ix);
        }
        signextend_stack_elem_ix(std::move(ix), src, {});
    }

    void Emitter::shl()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(shl(std::move(left), std::move(right), {}));
    }

    // Discharge through `shift_by_stack_elem`
    template <typename... LiveSet>
    StackElemRef Emitter::shl(
        StackElemRef shift, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal()->value;
            auto const &x = value->literal()->value;
            return stack_.alloc_literal({x << i});
        }

        return shift_by_stack_elem<ShiftType::SHL>(
            std::move(shift), std::move(value), live);
    }

    void Emitter::shr()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(shr(std::move(left), std::move(right), {}));
    }

    // Discharge through `shift_by_stack_elem`
    template <typename... LiveSet>
    StackElemRef Emitter::shr(
        StackElemRef shift, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal()->value;
            auto const &x = value->literal()->value;
            return stack_.alloc_literal({x >> i});
        }

        return shift_by_stack_elem<ShiftType::SHR>(
            std::move(shift), std::move(value), live);
    }

    void Emitter::sar()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(sar(std::move(left), std::move(right), {}));
    }

    // Discharge through `shift_by_stack_elem`
    template <typename... LiveSet>
    StackElemRef Emitter::sar(
        StackElemRef shift, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal()->value;
            auto const &x = value->literal()->value;
            return stack_.alloc_literal({monad::utils::sar(i, x)});
        }

        return shift_by_stack_elem<ShiftType::SAR>(
            std::move(shift), std::move(value), live);
    }

    void Emitter::and_()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(and_(std::move(left), std::move(right), {}));
    }

    // Discharge
    template <typename... LiveSet>
    StackElemRef Emitter::and_(
        StackElemRef pre_dst, StackElemRef pre_src,
        std::tuple<LiveSet...> const &live)
    {
        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                return stack_.alloc_literal({x & y});
            }
            // a & 1...1 ==> a
            if (pre_dst->literal()->value ==
                std::numeric_limits<uint256_t>::max()) {
                return pre_src;
            }
            // a & 0...0 ==> 0
            if (pre_dst->literal()->value == 0) {
                return stack_.alloc_literal({0});
            }
        }
        else if (pre_src->literal()) {
            // 1...1 & b ==> b
            if (pre_src->literal()->value ==
                std::numeric_limits<uint256_t>::max()) {
                return pre_dst;
            }
            // 0...0 & b ==> 0
            if (pre_src->literal()->value == 0) {
                return stack_.alloc_literal({0});
            }
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src), live);

        AVX_OR_GENERAL_BIN_INSTR(and_, vpand)
        (dst, left, left_loc, right, right_loc, [](size_t, uint64_t x) {
            return x == std::numeric_limits<uint64_t>::max();
        });

        return dst;
    }

    void Emitter::or_()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(or_(std::move(left), std::move(right), {}));
    }

    // Discharge
    template <typename... LiveSet>
    StackElemRef Emitter::or_(
        StackElemRef pre_dst, StackElemRef pre_src,
        std::tuple<LiveSet...> const &live)
    {
        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                return stack_.alloc_literal({x | y});
            }
            // a | 0...0 ==> a
            if (pre_dst->literal()->value == 0) {
                return pre_src;
            }
            // a | 1...1 ==> 1...1
            if (pre_dst->literal()->value ==
                std::numeric_limits<uint256_t>::max()) {
                return stack_.alloc_literal(
                    {std::numeric_limits<uint256_t>::max()});
            }
        }
        else if (pre_src->literal()) {
            // 0...0 & b ==> b
            if (pre_src->literal()->value == 0) {
                return pre_dst;
            }
            // 1...1 | b ==> 1...1
            if (pre_src->literal()->value ==
                std::numeric_limits<uint256_t>::max()) {
                return stack_.alloc_literal(
                    {std::numeric_limits<uint256_t>::max()});
            }
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src), live);

        AVX_OR_GENERAL_BIN_INSTR(or_, vpor)
        (dst, left, left_loc, right, right_loc, [](size_t, uint64_t x) {
            return x == 0;
        });

        return dst;
    }

    void Emitter::xor_()
    {
        auto left = stack_.pop();
        auto right = stack_.pop();
        stack_.push(xor_(std::move(left), std::move(right), {}));
    }

    // Discharge
    template <typename... LiveSet>
    StackElemRef Emitter::xor_(
        StackElemRef pre_dst, StackElemRef pre_src,
        std::tuple<LiveSet...> const &live)
    {
        if (pre_dst == pre_src) {
            return stack_.alloc_literal({0});
        }
        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal()->value;
            auto const &y = pre_src->literal()->value;
            return stack_.alloc_literal({x ^ y});
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src), live);

        AVX_OR_GENERAL_BIN_INSTR(xor_, vpxor)
        (dst, left, left_loc, right, right_loc, [](size_t, uint64_t) {
            return false;
        });

        return dst;
    }

    // Discharge
    void Emitter::eq()
    {
        auto pre_dst = stack_.pop();
        auto pre_src = stack_.pop();

        if (pre_dst == pre_src) {
            push(1);
            return;
        }
        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal()->value;
            auto const &y = pre_src->literal()->value;
            push(x == y);
            return;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src), {});

        AVX_OR_GENERAL_BIN_INSTR(xor_, vpxor)
        (dst, left, left_loc, right, right_loc, [](size_t, uint64_t) {
            return false;
        });

        if (left_loc == LocationType::AvxReg) {
            x86::Ymm const &y = avx_reg_to_ymm(*dst->avx_reg());
            as_.vptest(y, y);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(left_loc == LocationType::GeneralReg);
            Gpq256 const &gpq = general_reg_to_gpq256(*dst->general_reg());
            for (size_t i = 0; i < 3; ++i) {
                as_.or_(gpq[i + 1], gpq[i]);
            }
        }
        stack_.push_deferred_comparison(Comparison::Equal);
    }

    // Discharge, except when top element is deferred comparison
    void Emitter::iszero()
    {
        if (stack_.negate_top_deferred_comparison()) {
            return;
        }
        auto elem = stack_.pop();
        if (elem->literal()) {
            push(elem->literal()->value == 0);
            return;
        }
        discharge_deferred_comparison();
        auto [left, right, loc] = get_una_arguments(elem, std::nullopt, {});
        MONAD_COMPILER_DEBUG_ASSERT(left == right);
        if (loc == LocationType::AvxReg) {
            x86::Ymm const y = avx_reg_to_ymm(*left->avx_reg());
            as_.vptest(y, y);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(loc == LocationType::GeneralReg);
            Gpq256 const &gpq = general_reg_to_gpq256(*left->general_reg());
            for (size_t i = 0; i < 3; ++i) {
                as_.or_(gpq[i + 1], gpq[i]);
            }
        }
        stack_.push_deferred_comparison(Comparison::Equal);
    }

    // Discharge
    void Emitter::not_()
    {
        auto elem = stack_.pop();
        if (elem->literal()) {
            push(~elem->literal()->value);
            return;
        }

        discharge_deferred_comparison();

        auto [left, right, loc] =
            get_una_arguments(elem, stack_.top_index() + 1, {});
        if (loc == LocationType::AvxReg) {
            x86::Ymm const y_left = avx_reg_to_ymm(*left->avx_reg());
            x86::Ymm const y_right = avx_reg_to_ymm(*right->avx_reg());
            auto lbl =
                append_literal(Literal{std::numeric_limits<uint256_t>::max()});
            as_.vpxor(y_left, y_right, x86::ptr(lbl));
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(loc == LocationType::GeneralReg);
            MONAD_COMPILER_DEBUG_ASSERT(left == right);
            Gpq256 const &gpq = general_reg_to_gpq256(*left->general_reg());
            for (size_t i = 0; i < 4; ++i) {
                as_.not_(gpq[i]);
            }
        }
        stack_.push(std::move(left));
    }

    // Discharge
    void Emitter::gas(int32_t remaining_base_gas)
    {
        MONAD_COMPILER_DEBUG_ASSERT(remaining_base_gas >= 0);
        discharge_deferred_comparison();
        auto [dst, _] = alloc_general_reg();
        Gpq256 const &gpq = general_reg_to_gpq256(*dst->general_reg());
        as_.mov(
            gpq[0], x86::qword_ptr(reg_context, context_offset_gas_remaining));
        as_.add(gpq[0], remaining_base_gas);
        as_.xor_(gpq[1], gpq[1]);
        as_.xor_(gpq[2], gpq[2]);
        as_.xor_(gpq[3], gpq[3]);
        stack_.push(std::move(dst));
    }

    // No discharge
    void Emitter::address()
    {
        read_context_address(context_offset_env_recipient);
    }

    // No discharge
    void Emitter::caller()
    {
        read_context_address(context_offset_env_sender);
    }

    // No discharge
    void Emitter::callvalue()
    {
        read_context_word(context_offset_env_value);
    }

    // No discharge
    void Emitter::calldatasize()
    {
        read_context_uint32_to_word(context_offset_env_input_data_size);
    }

    // No discharge
    void Emitter::returndatasize()
    {
        read_context_uint32_to_word(context_offset_env_return_data_size);
    }

    // No discharge
    void Emitter::msize()
    {
        read_context_uint32_to_word(context_offset_memory_size);
    }

    // No discharge
    void Emitter::codesize()
    {
        stack_.push_literal(bytecode_size_);
    }

    // No discharge
    void Emitter::origin()
    {
        read_context_address(context_offset_env_tx_context_origin);
    }

    // No discharge
    void Emitter::gasprice()
    {
        read_context_word(context_offset_env_tx_context_tx_gas_price);
    }

    // No discharge
    void Emitter::gaslimit()
    {
        read_context_uint64_to_word(
            context_offset_env_tx_context_block_gas_limit);
    }

    // No discharge
    void Emitter::coinbase()
    {
        read_context_address(context_offset_env_tx_context_block_coinbase);
    }

    // No discharge
    void Emitter::timestamp()
    {
        read_context_uint64_to_word(
            context_offset_env_tx_context_block_timestamp);
    }

    // No discharge
    void Emitter::number()
    {
        read_context_uint64_to_word(context_offset_env_tx_context_block_number);
    }

    // No discharge
    void Emitter::prevrandao()
    {
        read_context_word(context_offset_env_tx_context_block_prev_randao);
    }

    // No discharge
    void Emitter::chainid()
    {
        read_context_word(context_offset_env_tx_context_chain_id);
    }

    // No discharge
    void Emitter::basefee()
    {
        read_context_word(context_offset_env_tx_context_block_base_fee);
    }

    // No discharge
    void Emitter::blobbasefee()
    {
        read_context_word(context_offset_env_tx_context_blob_base_fee);
    }

    // Discharge
    void Emitter::call_runtime_impl(RuntimeImpl &rt)
    {
        discharge_deferred_comparison();
        spill_caller_save_regs(rt.spill_avx_regs());
        size_t const n = rt.explicit_arg_count();
        for (size_t i = 0; i < n; ++i) {
            rt.pass(stack_.pop());
        }
        rt.call_impl();
    }

    // Discharge
    void Emitter::jump()
    {
        discharge_deferred_comparison();
        jump_stack_elem_dest(stack_.pop(), {});
    }

    // Discharge indirectly with `jumpi_comparison`
    void Emitter::jumpi(uint256_t const &fallthrough)
    {
        MONAD_COMPILER_DEBUG_ASSERT(fallthrough <= bytecode_size_);
        if (jump_dests_.count(static_cast<byte_offset>(fallthrough))) {
            jumpi_spill_fallthrough_stack();
        }
        else {
            jumpi_keep_fallthrough_stack();
        }
    }

    // Discharge
    void Emitter::fallthrough()
    {
        discharge_deferred_comparison();
        write_to_final_stack_offsets();
        adjust_by_stack_delta<false>();
    }

    // No discharge
    void Emitter::stop()
    {
        status_code(runtime::StatusCode::Success);
        as_.jmp(epilogue_label_);
    }

    // No discharge
    void Emitter::invalid_instruction()
    {
        as_.jmp(error_label_);
    }

    // Discharge through `return_with_status_code`
    void Emitter::return_()
    {
        return_with_status_code(runtime::StatusCode::Success);
    }

    // Discharge through `return_with_status_code`
    void Emitter::revert()
    {
        return_with_status_code(runtime::StatusCode::Revert);
    }

    void Emitter::status_code(runtime::StatusCode status)
    {
        int32_t const c = static_cast<int32_t>(status);
        as_.mov(x86::qword_ptr(reg_context, context_offset_result_status), c);
    }

    void Emitter::error_block(asmjit::Label &lbl, runtime::StatusCode status)
    {
        as_.align(asmjit::AlignMode::kCode, 16);
        as_.bind(lbl);
        status_code(status);
        as_.jmp(epilogue_label_);
    }

    void Emitter::return_with_status_code(runtime::StatusCode status)
    {
        discharge_deferred_comparison();
        auto offset = stack_.pop();
        RegReserv const offset_avx_reserv{offset};
        auto size = stack_.pop();
        RegReserv const size_avx_reserv{size};
        status_code(status);
        mov_stack_elem_to_unaligned_mem(
            offset, qword_ptr(reg_context, context_offset_result_offset));
        mov_stack_elem_to_unaligned_mem(
            size, qword_ptr(reg_context, context_offset_result_size));
        as_.jmp(epilogue_label_);
    }

    template <typename... LiveSet>
    void Emitter::jump_stack_elem_dest(
        StackElemRef dest, std::tuple<LiveSet...> const &live)
    {
        if (dest->literal()) {
            auto lit = literal_jump_dest_operand(std::move(dest));
            write_to_final_stack_offsets();
            adjust_by_stack_delta<false>();
            jump_literal_dest(lit);
        }
        else {
            auto [op, spill_elem] = non_literal_jump_dest_operand(dest, live);
            write_to_final_stack_offsets();
            adjust_by_stack_delta<false>();
            jump_non_literal_dest(dest, op, spill_elem);
        }
    }

    uint256_t Emitter::literal_jump_dest_operand(StackElemRef dest)
    {
        return dest->literal()->value;
    }

    asmjit::Label const &Emitter::jump_dest_label(uint256_t const &dest)
    {
        if (dest >= bytecode_size_) {
            return error_label_;
        }
        else {
            auto it = jump_dests_.find(dest[0]);
            if (it == jump_dests_.end()) {
                return error_label_;
            }
            else {
                return it->second;
            }
        }
    }

    void Emitter::jump_literal_dest(uint256_t const &dest)
    {
        as_.jmp(jump_dest_label(dest));
    }

    template <typename... LiveSet>
    std::pair<Emitter::Operand, std::optional<StackElem *>>
    Emitter::non_literal_jump_dest_operand(
        StackElemRef const &dest, std::tuple<LiveSet...> const &live)
    {
        Operand op;
        std::optional<StackElem *> spill_elem;
        if (dest->stack_offset()) {
            if (is_live(dest, live)) {
                if (!dest->general_reg()) {
                    spill_elem =
                        revertible_mov_stack_offset_to_general_reg(dest);
                }
            }
            else if (dest->stack_offset()->offset <= stack_.top_index()) {
                if (!dest->general_reg()) {
                    spill_elem =
                        revertible_mov_stack_offset_to_general_reg(dest);
                }
            }
            else {
                op = stack_offset_to_mem(*dest->stack_offset());
            }
        }
        if (dest->general_reg()) {
            op = general_reg_to_gpq256(*dest->general_reg());
        }
        else if (!dest->stack_offset()) {
            MONAD_COMPILER_DEBUG_ASSERT(dest->avx_reg().has_value());
            x86::Mem const m = x86::qword_ptr(x86::rsp, -32);
            mov_avx_reg_to_unaligned_mem(*dest->avx_reg(), m);
            op = m;
        }
        return {op, spill_elem};
    }

    void Emitter::jump_non_literal_dest(
        StackElemRef dest, Operand const &dest_op,
        std::optional<StackElem *> spill_elem)
    {
        if (spill_elem.has_value()) {
            MONAD_COMPILER_DEBUG_ASSERT(dest->general_reg().has_value());
            // Restore `stack_` back to the state before calling
            // `non_literal_jump_dest_operand`.
            auto *e = *spill_elem;
            if (e != nullptr) {
                MONAD_COMPILER_DEBUG_ASSERT(e->is_on_stack());
                stack_.move_general_reg(*dest, *e);
                stack_.remove_stack_offset(*e);
            }
            else {
                stack_.remove_general_reg(*dest);
            }
        }
        if (std::holds_alternative<Gpq256>(dest_op)) {
            Gpq256 const &gpq = std::get<Gpq256>(dest_op);
            as_.cmp(gpq[0], bytecode_size_);
            as_.sbb(gpq[1], 0);
            as_.sbb(gpq[2], 0);
            as_.sbb(gpq[3], 0);
            as_.jnb(error_label_);
            as_.lea(x86::rax, x86::ptr(jump_table_label_));
            as_.jmp(x86::ptr(x86::rax, gpq[0], 3));
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(
                std::holds_alternative<x86::Mem>(dest_op));
            x86::Mem m = std::get<x86::Mem>(dest_op);
            if (m.baseReg() == x86::rbp) {
                // Since `adjust_by_stack_delta` has been called before this
                // function, we need to adjust when accessing EVM stack memory.
                m.addOffset(-(stack_.delta() * 32));
            }
            // Register rcx is available, because `block_prologue` has
            // already written stack elements to their final stack offsets.
            as_.mov(x86::rcx, m);
            as_.cmp(x86::rcx, bytecode_size_);
            for (size_t i = 1; i < 4; ++i) {
                m.addOffset(8);
                as_.sbb(m, 0);
            }
            as_.jnb(error_label_);
            as_.lea(x86::rax, x86::ptr(jump_table_label_));
            as_.jmp(x86::ptr(x86::rax, x86::rcx, 3));
        }
    }

    void Emitter::conditional_jmp(asmjit::Label const &lbl, Comparison comp)
    {
        switch (comp) {
        case Comparison::Below:
            as_.jb(lbl);
            break;
        case Comparison::AboveEqual:
            as_.jae(lbl);
            break;
        case Comparison::Above:
            as_.ja(lbl);
            break;
        case Comparison::BelowEqual:
            as_.jbe(lbl);
            break;
        case Comparison::Less:
            as_.jl(lbl);
            break;
        case Comparison::GreaterEqual:
            as_.jge(lbl);
            break;
        case Comparison::Greater:
            as_.jg(lbl);
            break;
        case Comparison::LessEqual:
            as_.jle(lbl);
            break;
        case Comparison::Equal:
            as_.je(lbl);
            break;
        case Comparison::NotEqual:
            as_.jne(lbl);
            break;
        }
    }

    Comparison Emitter::jumpi_comparison(StackElemRef cond, StackElemRef dest)
    {
        auto dc = stack_.discharge_deferred_comparison();
        if (dc.stack_elem && (dc.stack_elem == dest.get() ||
                              !dc.stack_elem->stack_indices().empty())) {
            discharge_deferred_comparison(dc.stack_elem, dc.comparison);
        }
        if (dc.negated_stack_elem &&
            (dc.negated_stack_elem == dest.get() ||
             !dc.negated_stack_elem->stack_indices().empty())) {
            discharge_deferred_comparison(
                dc.negated_stack_elem, negate_comparison(dc.comparison));
        }

        Comparison comp;
        if (cond.get() == dc.stack_elem) {
            comp = dc.comparison;
        }
        else if (cond.get() == dc.negated_stack_elem) {
            comp = negate_comparison(dc.comparison);
        }
        else {
            comp = Comparison::NotEqual;
            if (cond->stack_offset() && !cond->avx_reg()) {
                mov_stack_offset_to_avx_reg(cond);
            }
            if (cond->avx_reg()) {
                auto y = avx_reg_to_ymm(*cond->avx_reg());
                as_.vptest(y, y);
            }
            else {
                MONAD_COMPILER_DEBUG_ASSERT(cond->general_reg().has_value());
                Gpq256 const &gpq = general_reg_to_gpq256(*cond->general_reg());
                if (!is_live(cond, std::make_tuple(dest))) {
                    as_.or_(gpq[1], gpq[0]);
                    as_.or_(gpq[2], gpq[1]);
                    as_.or_(gpq[3], gpq[2]);
                }
                else {
                    as_.mov(x86::rax, gpq[0]);
                    as_.or_(x86::rax, gpq[1]);
                    as_.or_(x86::rax, gpq[2]);
                    as_.or_(x86::rax, gpq[3]);
                }
            }
        }
        return comp;
    }

    void Emitter::jumpi_spill_fallthrough_stack()
    {
        auto dest = stack_.pop();
        auto cond = stack_.pop();
        if (cond->literal()) {
            discharge_deferred_comparison();
            if (cond->literal()->value == 0) {
                // Clear to remove locations, if not on stack:
                cond = nullptr;
                dest = nullptr;
                write_to_final_stack_offsets();
                adjust_by_stack_delta<false>();
            }
            else {
                // Clear to remove locations, if not on stack:
                cond = nullptr;
                jump_stack_elem_dest(std::move(dest), {});
            }
            return;
        }

        Comparison const comp = jumpi_comparison(std::move(cond), dest);

        if (dest->literal()) {
            auto lit = literal_jump_dest_operand(std::move(dest));
            write_to_final_stack_offsets();
            adjust_by_stack_delta<true>();
            conditional_jmp(jump_dest_label(lit), comp);
        }
        else {
            auto fallthrough_lbl = as_.newLabel();
            // Note that `cond` is not live here.
            auto [op, spill_elem] = non_literal_jump_dest_operand(dest, {});
            write_to_final_stack_offsets();
            conditional_jmp(fallthrough_lbl, negate_comparison(comp));
            adjust_by_stack_delta<false>();
            jump_non_literal_dest(dest, op, spill_elem);
            as_.bind(fallthrough_lbl);
            adjust_by_stack_delta<false>();
        }
    }

    void Emitter::jumpi_keep_fallthrough_stack()
    {
        keep_stack_in_next_block_ = true;

        auto dest = stack_.pop();
        auto cond = stack_.pop();

        if (cond->literal()) {
            discharge_deferred_comparison();
            if (cond->literal()->value != 0) {
                // Clear to remove locations, if not on stack:
                cond = nullptr;
                jump_stack_elem_dest(std::move(dest), {});
            }
            return;
        }

        Comparison const comp = jumpi_comparison(std::move(cond), dest);
        if (dest->literal() && stack_.delta() == 0 &&
            stack_.is_properly_spilled()) {
            // We do not need to spill stack elements and we do not need
            // to adjust by stack delta, so only need conditional jump.
            auto lit = literal_jump_dest_operand(std::move(dest));
            conditional_jmp(jump_dest_label(lit), comp);
        }
        else {
            asmjit::Label const fallthrough_lbl = as_.newLabel();
            conditional_jmp(fallthrough_lbl, negate_comparison(comp));
            // The jump_stack_elem_dest function will spill to stack
            // and/or adjust by stack delta.
            jump_stack_elem_dest(std::move(dest), {});
            as_.bind(fallthrough_lbl);
        }
    }

    void Emitter::read_context_address(int32_t offset)
    {
        x86::Mem m = x86::qword_ptr(reg_context, offset);
        auto [dst, _] = alloc_general_reg();
        Gpq256 const &gpq = general_reg_to_gpq256(*dst->general_reg());

        m.setSize(4);
        as_.mov(gpq[2].r32(), m);
        m.addOffset(4);
        m.setSize(8);
        as_.mov(gpq[1], m);
        m.addOffset(8);
        as_.mov(gpq[0], m);
        if (stack_.has_deferred_comparison()) {
            as_.mov(gpq[3], 0);
        }
        else {
            as_.xor_(gpq[3], gpq[3]);
        }
        as_.bswap(gpq[2].r32());
        as_.bswap(gpq[1]);
        as_.bswap(gpq[0]);
        stack_.push(std::move(dst));
    }

    void Emitter::read_context_word(int32_t offset)
    {
        x86::Mem const m = x86::qword_ptr(reg_context, offset);
        auto [dst, _] = alloc_avx_reg();
        auto y = avx_reg_to_ymm(*dst->avx_reg());
        as_.vmovups(y, m);
        auto const &lbl = append_literal(Literal{uint256_t{
            0x0001020304050607,
            0x08090a0b0c0d0e0f,
            0x0001020304050607,
            0x08090a0b0c0d0e0f}});
        // Permute bytes in avx register y:
        // {b0, ..., b7, b8, ..., b15, b16, ..., b23, b24, ..., b31} ->
        // {b7, ..., b0, b15, ..., b8, b23, ..., b16, b31, ..., b24}
        as_.vpshufb(y, y, x86::ptr(lbl));
        // Permute qwords in avx register y:
        // {b7, ..., b0, b15, ..., b8, b23, ..., b16, b31, ..., b24} ->
        // {b31, ..., b24, b23, ..., b16, b15, ..., b8, b7, ..., b0}
        as_.vpermq(y, y, 27);
        stack_.push(std::move(dst));
    }

    void Emitter::read_context_uint32_to_word(int32_t offset)
    {
        auto [dst, _] = alloc_general_reg();
        Gpq256 const &gpq = general_reg_to_gpq256(*dst->general_reg());
        as_.mov(gpq[0].r32(), x86::dword_ptr(reg_context, offset));
        if (stack_.has_deferred_comparison()) {
            as_.mov(gpq[1], 0);
            as_.mov(gpq[2], 0);
            as_.mov(gpq[3], 0);
        }
        else {
            as_.xor_(gpq[1], gpq[1]);
            as_.xor_(gpq[2], gpq[2]);
            as_.xor_(gpq[3], gpq[3]);
        }
        stack_.push(std::move(dst));
    }

    void Emitter::read_context_uint64_to_word(int32_t offset)
    {
        auto [dst, _] = alloc_general_reg();
        Gpq256 const &gpq = general_reg_to_gpq256(*dst->general_reg());
        as_.mov(gpq[0], x86::qword_ptr(reg_context, offset));
        if (stack_.has_deferred_comparison()) {
            as_.mov(gpq[1], 0);
            as_.mov(gpq[2], 0);
            as_.mov(gpq[3], 0);
        }
        else {
            as_.xor_(gpq[1], gpq[1]);
            as_.xor_(gpq[2], gpq[2]);
            as_.xor_(gpq[3], gpq[3]);
        }
        stack_.push(std::move(dst));
    }

    void Emitter::lt(StackElemRef pre_dst, StackElemRef pre_src)
    {
        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal()->value;
            auto const &y = pre_src->literal()->value;
            push(x < y);
            return;
        }
        discharge_deferred_comparison();
        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<false>(
            std::move(pre_dst), std::nullopt, std::move(pre_src), {});
        cmp(std::move(dst), dst_loc, std::move(src), src_loc);
        stack_.push_deferred_comparison(Comparison::Below);
    }

    void Emitter::slt(StackElemRef pre_dst, StackElemRef pre_src)
    {
        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal()->value;
            auto const &y = pre_src->literal()->value;
            push(intx::slt(x, y));
            return;
        }
        discharge_deferred_comparison();
        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<false>(
            std::move(pre_dst), std::nullopt, std::move(pre_src), {});
        cmp(std::move(dst), dst_loc, std::move(src), src_loc);
        stack_.push_deferred_comparison(Comparison::Less);
    }

    void Emitter::cmp(
        StackElemRef dst, LocationType dst_loc, StackElemRef src,
        LocationType src_loc)
    {
        GENERAL_BIN_INSTR(cmp, sbb)
        (dst, dst_loc, src, src_loc, [](size_t, uint64_t) { return false; });
    }

    void Emitter::byte_literal_ix(uint256_t const &ix, StackOffset src)
    {
        if (ix >= 32) {
            return push(0);
        }
        int64_t const i = 31 - static_cast<int64_t>(ix[0]);

        auto [dst, dst_reserv] = alloc_general_reg();
        Gpq256 const &gpq = general_reg_to_gpq256(*dst->general_reg());

        as_.xor_(gpq[0], gpq[0]);
        as_.xor_(gpq[1], gpq[1]);
        as_.xor_(gpq[2], gpq[2]);
        as_.xor_(gpq[3], gpq[3]);
        auto m = stack_offset_to_mem(src);
        m.addOffset(i);
        as_.mov(gpq[0].r8Lo(), m);

        stack_.push(std::move(dst));
    }

    void Emitter::byte_general_reg_or_stack_offset_ix(
        StackElemRef ix, StackOffset src)
    {
        auto [dst, dst_reserv] = alloc_general_reg();

        Gpq256 const &dst_gpq = general_reg_to_gpq256(*dst->general_reg());

        as_.mov(dst_gpq[0], 31);
        as_.xor_(dst_gpq[1], dst_gpq[1]);
        as_.xor_(dst_gpq[2], dst_gpq[2]);
        as_.xor_(dst_gpq[3], dst_gpq[3]);
        if (ix->general_reg()) {
            Gpq256 const &ix_gpq = general_reg_to_gpq256(*ix->general_reg());
            as_.sub(dst_gpq[0], ix_gpq[0]);
            as_.sbb(dst_gpq[1], ix_gpq[1]);
            as_.sbb(dst_gpq[2], ix_gpq[2]);
            as_.sbb(dst_gpq[3], ix_gpq[3]);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(ix->stack_offset().has_value());
            x86::Mem m = stack_offset_to_mem(*ix->stack_offset());
            as_.sub(dst_gpq[0], m);
            for (size_t i = 1; i < 4; ++i) {
                m.addOffset(8);
                as_.sbb(dst_gpq[i], m);
            }
        }
        auto byte_out_of_bounds_lbl = as_.newLabel();
        auto byte_after_lbl = as_.newLabel();
        as_.jb(byte_out_of_bounds_lbl);
        auto m = stack_offset_to_mem(src);
        m.setIndex(dst_gpq[0]);
        as_.mov(dst_gpq[0].r8Lo(), m);
        as_.bind(byte_after_lbl);

        byte_out_of_bounds_handlers_.emplace_back(
            byte_out_of_bounds_lbl, dst_gpq, byte_after_lbl);

        stack_.push(std::move(dst));
    }

    template <typename... LiveSet>
    bool Emitter::cmp_stack_elem_to_int32(
        StackElemRef e, int32_t i, x86::Mem flag,
        std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!e->literal().has_value());
        flag.setSize(4);
        if (e->general_reg()) {
            auto const &gpq = general_reg_to_gpq256(*e->general_reg());
            as_.cmp(gpq[0], i);
            if (!is_live(e, live)) {
                for (size_t i = 1; i < 4; ++i) {
                    as_.sbb(gpq[i], 0);
                }
                return true;
            }
            else {
                as_.mov(x86::rax, gpq[1]);
                as_.cmovnb(x86::rax, flag);
                as_.or_(x86::rax, gpq[2]);
                as_.or_(x86::rax, gpq[3]);
                return false;
            }
        }
        else {
            if (!e->stack_offset()) {
                mov_avx_reg_to_stack_offset(e);
            }
            x86::Mem mem = stack_offset_to_mem(*e->stack_offset());
            as_.cmp(mem, i);
            if (!is_live(e, live)) {
                for (size_t i = 1; i < 4; ++i) {
                    mem.addOffset(8);
                    as_.sbb(mem, 0);
                }
                return true;
            }
            else {
                mem.addOffset(8);
                as_.mov(x86::rax, mem);
                as_.cmovnb(x86::rax, flag);
                mem.addOffset(8);
                as_.or_(x86::rax, mem);
                mem.addOffset(8);
                as_.or_(x86::rax, mem);
                return false;
            }
        }
    }

    void Emitter::signextend_literal_ix(uint256_t const &ix, StackElemRef src)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!src->literal().has_value());

        if (ix >= 31) {
            return stack_.push(std::move(src));
        }

        int32_t const byte_ix = static_cast<int32_t>(ix);
        int32_t const stack_ix = -byte_ix - 33;

        mov_stack_elem_to_unaligned_mem(src, x86::ptr(x86::rsp, stack_ix));

        auto [dst, dst_reserv] = alloc_avx_reg();
        auto dst_ymm = avx_reg_to_ymm(*dst->avx_reg());

        // Broadcast sign byte to all bytes in `dst_ymm`:
        as_.vpbroadcastb(dst_ymm, x86::byte_ptr(x86::rsp, -33));
        // Shift arithmetic right to fill `dst_ymm` with sign bit:
        as_.vpsraw(dst_ymm, dst_ymm, 15);
        // Override most significant bytes of `src` on the stack:
        as_.vmovups(x86::ptr(x86::rsp, -32), dst_ymm);
        // Load the result:
        as_.vmovups(dst_ymm, x86::ptr(x86::rsp, stack_ix));

        stack_.push(std::move(dst));
    }

    template <typename... LiveSet>
    void Emitter::signextend_stack_elem_ix(
        StackElemRef ix, StackElemRef src, std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!ix->literal().has_value());

        auto const &bound_lbl = append_literal(Literal{31});
        auto bound_mem = x86::qword_ptr(bound_lbl);
        bool const nb =
            cmp_stack_elem_to_int32(ix, 32, bound_mem, std::make_tuple(src));

        x86::Mem stack_mem;
        if (ix->general_reg()) {
            auto const &gpq = general_reg_to_gpq256(*ix->general_reg());
            auto byte_ix = gpq[0];
            if (is_live(ix, std::tuple_cat(std::make_tuple(src), live))) {
                byte_ix = x86::rax;
                as_.mov(byte_ix, gpq[0]);
            }
            if (nb) {
                as_.cmovnb(byte_ix, bound_mem);
            }
            else {
                as_.cmovnz(byte_ix, bound_mem);
            }
            as_.neg(byte_ix);
            stack_mem = x86::qword_ptr(x86::rsp, byte_ix, 0, -33);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(ix->stack_offset().has_value());
            auto mem = stack_offset_to_mem(*ix->stack_offset());
            as_.mov(x86::eax, mem);
            if (nb) {
                as_.cmovnb(x86::eax, bound_mem);
            }
            else {
                as_.cmovnz(x86::eax, bound_mem);
            }
            as_.neg(x86::rax);
            stack_mem = x86::qword_ptr(x86::rsp, x86::rax, 0, -33);
        }

        mov_stack_elem_to_unaligned_mem(src, stack_mem);

        auto [dst, dst_reserv] = alloc_avx_reg();
        auto dst_ymm = avx_reg_to_ymm(*dst->avx_reg());

        // See `signextend_literal_ix`
        as_.vpbroadcastb(dst_ymm, x86::byte_ptr(x86::rsp, -33));
        as_.vpsraw(dst_ymm, dst_ymm, 15);
        as_.vmovups(x86::ptr(x86::rsp, -32), dst_ymm);
        as_.vmovups(dst_ymm, stack_mem);

        stack_.push(std::move(dst));
    }

    // Discharge directly or through `shift_by_literal`.
    template <Emitter::ShiftType shift_type, typename... LiveSet>
    StackElemRef Emitter::shift_by_stack_elem(
        StackElemRef shift, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        if (shift->literal()) {
            auto shift_value = std::move(shift)->literal()->value;
            return shift_by_literal<shift_type>(
                shift_value, std::move(value), live);
        }
        discharge_deferred_comparison();
        if (shift->general_reg()) {
            return shift_by_general_reg_or_stack_offset<shift_type>(
                std::move(shift), std::move(value), live);
        }
        if (!shift->stack_offset()) {
            mov_avx_reg_to_stack_offset(shift);
        }
        return shift_by_general_reg_or_stack_offset<shift_type>(
            std::move(shift), std::move(value), live);
    }

    template <Emitter::ShiftType shift_type, typename... LiveSet>
    void Emitter::setup_shift_stack(
        StackElemRef value, int32_t additional_byte_count,
        std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(additional_byte_count <= 32);

        if constexpr (shift_type == ShiftType::SHL) {
            if (additional_byte_count <= 8) {
                as_.mov(qword_ptr(x86::rsp, -40), 0);
            }
            else {
                mov_literal_to_unaligned_mem(
                    Literal{0}, qword_ptr(x86::rsp, -64));
            }
            mov_stack_elem_to_unaligned_mem(value, qword_ptr(x86::rsp, -32));
        }
        else if constexpr (shift_type == ShiftType::SHR) {
            mov_stack_elem_to_unaligned_mem(value, qword_ptr(x86::rsp, -64));
            if (additional_byte_count <= 8) {
                as_.mov(qword_ptr(x86::rsp, -32), 0);
            }
            else {
                mov_literal_to_unaligned_mem(
                    Literal{0}, qword_ptr(x86::rsp, -32));
            }
        }
        else {
            static_assert(shift_type == ShiftType::SAR);
            mov_stack_elem_to_unaligned_mem(value, qword_ptr(x86::rsp, -64));
            auto reg = x86::rax;
            if (value->general_reg()) {
                if (is_live(value, live)) {
                    as_.mov(
                        reg, general_reg_to_gpq256(*value->general_reg())[3]);
                }
                else {
                    reg = general_reg_to_gpq256(*value->general_reg())[3];
                }
            }
            else {
                as_.mov(reg, qword_ptr(x86::rsp, -40));
            }
            as_.sar(reg, 63);
            auto temp = x86::qword_ptr(x86::rsp, -32);
            for (uint8_t i = 0; i < additional_byte_count; i += 8) {
                as_.mov(temp, reg);
                temp.addOffset(8);
            }
        }
    }

    // Discharge
    template <Emitter::ShiftType shift_type, typename... LiveSet>
    StackElemRef Emitter::shift_by_literal(
        uint256_t const &shift_literal, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!value->literal().has_value());

        RegReserv const value_reserv{value};

        auto shift = shift_literal;
        if (shift >= 256) {
            if constexpr (
                shift_type == ShiftType::SHL || shift_type == ShiftType::SHR) {
                return stack_.alloc_literal({0});
            }
            else {
                shift = 256;
            }
        }
        else if (shift == 0) {
            return value;
        }

        discharge_deferred_comparison();

        int32_t const s = static_cast<int32_t>(shift[0]);
        int8_t const c = static_cast<int8_t>(s & 7);
        int32_t const d = s >> 3;

        if (d > 0) {
            setup_shift_stack<shift_type>(std::move(value), d, live);
        }

        // We do not need the register reservation for `dst`, because
        // we do not allocate any registers below.
        auto dst = [&] {
            if (d > 0) {
                if (c > 0) {
                    auto [r, _] = alloc_general_reg();
                    return r;
                }
                else {
                    auto [r, _] = alloc_avx_reg();
                    return r;
                }
            }
            else {
                MONAD_COMPILER_DEBUG_ASSERT(c > 0);
                if (!is_live(value, live) && value->general_reg()) {
                    auto r = stack_.release_general_reg(std::move(value));
                    return r;
                }
                else {
                    auto [r, _] = alloc_general_reg();
                    mov_stack_elem_to_gpq256(
                        std::move(value),
                        general_reg_to_gpq256(*r->general_reg()));
                    return r;
                }
            }
        }();

        if (c == 0) {
            MONAD_COMPILER_DEBUG_ASSERT(d > 0);
            MONAD_COMPILER_DEBUG_ASSERT(dst->avx_reg().has_value());
            x86::Ymm const dst_ymm = avx_reg_to_ymm(*dst->avx_reg());
            if constexpr (shift_type == ShiftType::SHL) {
                as_.vmovups(dst_ymm, x86::byte_ptr(x86::rsp, -32 - d));
            }
            else {
                as_.vmovups(dst_ymm, x86::qword_ptr(x86::rsp, d - 64));
            }
            return dst;
        }

        MONAD_COMPILER_DEBUG_ASSERT(dst->general_reg().has_value());

        Gpq256 const &dst_gpq = general_reg_to_gpq256(*dst->general_reg());
        if constexpr (shift_type == ShiftType::SHL) {
            if (d > 0) {
                as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, -8 - d));
                as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, -16 - d));
                as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, -24 - d));
                as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, -32 - d));
            }
            as_.shld(dst_gpq[3], dst_gpq[2], c);
            as_.shld(dst_gpq[2], dst_gpq[1], c);
            as_.shld(dst_gpq[1], dst_gpq[0], c);
            as_.shl(dst_gpq[0], c);
        }
        else {
            if (d > 0) {
                as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, d - 40));
                as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, d - 48));
                as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, d - 56));
                as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, d - 64));
            }
            as_.shrd(dst_gpq[0], dst_gpq[1], c);
            as_.shrd(dst_gpq[1], dst_gpq[2], c);
            as_.shrd(dst_gpq[2], dst_gpq[3], c);
            if constexpr (shift_type == ShiftType::SHR) {
                as_.shr(dst_gpq[3], c);
            }
            else {
                static_assert(shift_type == ShiftType::SAR);
                as_.sar(dst_gpq[3], c);
            }
        }

        return dst;
    }

    // Discharge
    template <Emitter::ShiftType shift_type, typename... LiveSet>
    StackElemRef Emitter::shift_by_general_reg_or_stack_offset(
        StackElemRef shift, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        if (value->literal()) {
            MONAD_COMPILER_DEBUG_ASSERT(!shift->literal().has_value());
            if (value->literal()->value == 0) {
                return value;
            }
            if constexpr (shift_type == ShiftType::SAR) {
                if (value->literal()->value ==
                    std::numeric_limits<uint256_t>::max()) {
                    return value;
                }
            }
        }

        RegReserv const shift_reserv{shift};
        RegReserv const value_reserv{value};

        discharge_deferred_comparison();

        setup_shift_stack<shift_type>(
            std::move(value), 32, std::tuple_cat(std::make_tuple(shift), live));

        auto [dst, dst_reserv] = alloc_general_reg();
        Gpq256 &dst_gpq = general_reg_to_gpq256(*dst->general_reg());

        auto const &bound_lbl = append_literal(Literal{256});
        bool const nb =
            cmp_stack_elem_to_int32(shift, 257, x86::qword_ptr(bound_lbl), {});

        // We only need to preserve rcx if it is in a stack element which is
        // currently on the virtual stack.
        // Note that rcx may be used by stack element `value`, `shift` or `dst`.
        bool preserve_rcx = stack_.is_general_reg_on_stack(rcx_general_reg);
        if (preserve_rcx &&
            dst->general_reg()->reg != CALLEE_SAVE_GENERAL_REG_ID) {
            MONAD_COMPILER_DEBUG_ASSERT(*dst->general_reg() != rcx_general_reg);
            // Make rcx part of the `dst` stack element, then we do not need to
            // preserve it. This saves one mov instruction.
            as_.mov(dst_gpq[rcx_general_reg_index], x86::rcx);
            static_assert(CALLEE_SAVE_GENERAL_REG_ID == 0);
            std::swap(
                gpq256_regs_[1][rcx_general_reg_index],
                gpq256_regs_[2][rcx_general_reg_index]);
            rcx_general_reg = *dst->general_reg();
            preserve_rcx = false;
        }

        if (preserve_rcx) {
            MONAD_COMPILER_DEBUG_ASSERT(
                dst->general_reg()->reg == CALLEE_SAVE_GENERAL_REG_ID);
            as_.mov(x86::rax, x86::rcx);
        }

        bool const dst_has_rcx = dst_gpq[rcx_general_reg_index] == x86::rcx;

        uint8_t const last_i = [&] {
            if constexpr (shift_type == ShiftType::SHL) {
                return 0;
            }
            return 3;
        }();
        if (dst_has_rcx) {
            std::swap(dst_gpq[last_i], dst_gpq[rcx_general_reg_index]);
            rcx_general_reg_index = last_i;
            MONAD_COMPILER_DEBUG_ASSERT(
                rcx_general_reg_index == 0 || rcx_general_reg_index == 3);
        }

        auto cmp_reg = x86::rcx;
        if (shift->general_reg()) {
            auto const &gpq = general_reg_to_gpq256(*shift->general_reg());
            // Note that `value` is not live here.
            if (is_live(shift, live)) {
                if (cmp_reg != gpq[0]) {
                    as_.mov(cmp_reg, gpq[0]);
                }
            }
            else {
                cmp_reg = gpq[0];
            }
        }
        else {
            auto mem = stack_offset_to_mem(*shift->stack_offset());
            as_.mov(cmp_reg, mem);
        }
        if (nb) {
            as_.cmovnb(cmp_reg, x86::qword_ptr(bound_lbl));
        }
        else {
            as_.cmovnz(cmp_reg, x86::qword_ptr(bound_lbl));
        }

        x86::Gpq offset_reg;
        if (cmp_reg != x86::rcx) {
            MONAD_COMPILER_DEBUG_ASSERT(!is_live(shift, live));
            offset_reg = cmp_reg;
            as_.mov(x86::cl, cmp_reg.r8Lo());
        }
        else {
            if (dst_has_rcx) {
                MONAD_COMPILER_DEBUG_ASSERT(
                    dst->general_reg()->reg != CALLEE_SAVE_GENERAL_REG_ID);
                offset_reg = x86::rax;
            }
            else {
                offset_reg = dst_gpq[last_i];
            }
            as_.mov(offset_reg, x86::rcx);
        }
        as_.shr(offset_reg.r16(), 3);
        as_.and_(x86::cl, 7);

        if constexpr (shift_type == ShiftType::SHL) {
            as_.neg(offset_reg);
            as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, offset_reg, 0, -8));
            as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, offset_reg, 0, -16));
            as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, offset_reg, 0, -24));
            as_.mov(offset_reg, x86::qword_ptr(x86::rsp, offset_reg, 0, -32));
            as_.shld(dst_gpq[3], dst_gpq[2], x86::cl);
            as_.shld(dst_gpq[2], dst_gpq[1], x86::cl);
            as_.shld(dst_gpq[1], offset_reg, x86::cl);
            as_.shlx(dst_gpq[0], offset_reg, x86::cl);
        }
        else {
            as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, offset_reg, 0, -64));
            as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, offset_reg, 0, -56));
            as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, offset_reg, 0, -48));
            as_.mov(offset_reg, x86::qword_ptr(x86::rsp, offset_reg, 0, -40));
            as_.shrd(dst_gpq[0], dst_gpq[1], x86::cl);
            as_.shrd(dst_gpq[1], dst_gpq[2], x86::cl);
            as_.shrd(dst_gpq[2], offset_reg, x86::cl);
            if constexpr (shift_type == ShiftType::SHR) {
                as_.shrx(dst_gpq[3], offset_reg, x86::cl);
            }
            else {
                static_assert(shift_type == ShiftType::SAR);
                as_.sarx(dst_gpq[3], offset_reg, x86::cl);
            }
        }

        if (preserve_rcx) {
            as_.mov(x86::rcx, x86::rax);
        }

        return dst;
    }

    template <bool commutative, typename... LiveSet>
    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::prepare_general_dest_and_source(
        StackElemRef dst, std::optional<int32_t> dst_ix, StackElemRef src,
        std::tuple<LiveSet...> const &live)
    {
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if constexpr (commutative) {
            if (dst->literal() && !dst->stack_offset() && !dst->avx_reg() &&
                !dst->general_reg()) {
                if (src->general_reg()) {
                    std::swap(dst, src);
                }
                else if (
                    (src->stack_offset() || src->avx_reg()) &&
                    is_literal_bounded(*dst->literal())) {
                    std::swap(dst, src);
                }
            }
        }

        if (dst.get() == src.get()) {
            if (!dst->general_reg()) {
                mov_stack_elem_to_general_reg(dst);
            }
            return {
                std::move(dst),
                LocationType::GeneralReg,
                std::move(src),
                LocationType::GeneralReg};
        }

        if (dst_ix && dst->stack_offset() && !is_live(dst, live)) {
            if (src->general_reg()) {
                return {
                    std::move(dst),
                    LocationType::StackOffset,
                    std::move(src),
                    LocationType::GeneralReg};
            }
            if (src->literal() && is_literal_bounded(*src->literal())) {
                return {
                    std::move(dst),
                    LocationType::StackOffset,
                    std::move(src),
                    LocationType::Literal};
            }
        }

        if (dst->general_reg()) {
            if (src->general_reg()) {
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::GeneralReg};
            }
            if (src->stack_offset()) {
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::StackOffset};
            }
            if (src->literal()) {
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::Literal};
            }
        }

        if (dst->stack_offset()) {
            if (src->general_reg()) {
                return {
                    std::move(dst),
                    LocationType::StackOffset,
                    std::move(src),
                    LocationType::GeneralReg};
            }
            if (src->literal() && is_literal_bounded(*src->literal())) {
                return {
                    std::move(dst),
                    LocationType::StackOffset,
                    std::move(src),
                    LocationType::Literal};
            }
        }

        if (dst->general_reg()) {
            MONAD_COMPILER_DEBUG_ASSERT(src->avx_reg().has_value());
            mov_avx_reg_to_stack_offset(src);
            return {
                std::move(dst),
                LocationType::GeneralReg,
                std::move(src),
                LocationType::StackOffset};
        }

        MONAD_COMPILER_DEBUG_ASSERT(!dst->general_reg().has_value());

        if (!dst->stack_offset()) {
            if (dst->literal()) {
                mov_literal_to_general_reg(dst);
                if (src->general_reg()) {
                    return {
                        std::move(dst),
                        LocationType::GeneralReg,
                        std::move(src),
                        LocationType::GeneralReg};
                }
                if (src->stack_offset()) {
                    return {
                        std::move(dst),
                        LocationType::GeneralReg,
                        std::move(src),
                        LocationType::StackOffset};
                }
                if (src->literal()) {
                    return {
                        std::move(dst),
                        LocationType::GeneralReg,
                        std::move(src),
                        LocationType::Literal};
                }
                mov_avx_reg_to_stack_offset(src);
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::StackOffset};
            }
            else {
                if (dst_ix) {
                    mov_avx_reg_to_stack_offset(dst, *dst_ix);
                }
                else {
                    mov_avx_reg_to_stack_offset(dst);
                }
                // fall through
            }
        }

        if (src->general_reg()) {
            return {
                std::move(dst),
                LocationType::StackOffset,
                std::move(src),
                LocationType::GeneralReg};
        }
        if (src->literal() && is_literal_bounded(*src->literal())) {
            return {
                std::move(dst),
                LocationType::StackOffset,
                std::move(src),
                LocationType::Literal};
        }
        if (src->stack_offset()) {
            if (is_live(dst, live) || !is_live(src, live)) {
                mov_stack_offset_to_general_reg(dst);
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::StackOffset};
            }
            else {
                mov_stack_offset_to_general_reg(src);
                return {
                    std::move(dst),
                    LocationType::StackOffset,
                    std::move(src),
                    LocationType::GeneralReg};
            }
        }

        if (src->literal()) {
            if (is_live(dst, live) || !is_live(src, live)) {
                mov_stack_offset_to_general_reg(dst);
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::Literal};
            }
            else {
                mov_literal_to_general_reg(src);
                return {
                    std::move(dst),
                    LocationType::StackOffset,
                    std::move(src),
                    LocationType::GeneralReg};
            }
        }

        mov_stack_offset_to_general_reg(dst);
        mov_avx_reg_to_stack_offset(src);
        return {
            std::move(dst),
            LocationType::GeneralReg,
            std::move(src),
            LocationType::StackOffset};
    }

    template <bool commutative, typename... LiveSet>
    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::get_general_dest_and_source(
        StackElemRef dst_in, std::optional<int32_t> dst_ix, StackElemRef src_in,
        std::tuple<LiveSet...> const &live)
    {
        auto [dst, dst_loc, src, src_loc] =
            prepare_general_dest_and_source<commutative>(
                std::move(dst_in), dst_ix, std::move(src_in), live);
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if (dst_loc == LocationType::GeneralReg) {
            if (is_live(dst, live) && !dst->stack_offset() && !dst->literal() &&
                !dst->avx_reg()) {
                mov_general_reg_to_stack_offset(dst);
            }
            auto new_dst = stack_.release_general_reg(dst);
            if (dst.get() == src.get()) {
                return {new_dst, dst_loc, new_dst, src_loc};
            }
            else {
                return {std::move(new_dst), dst_loc, std::move(src), src_loc};
            }
        }
        MONAD_COMPILER_DEBUG_ASSERT(dst.get() != src.get());
        MONAD_COMPILER_DEBUG_ASSERT(dst_loc == LocationType::StackOffset);
        if (is_live(dst, live) && !dst->general_reg() && !dst->literal() &&
            !dst->avx_reg()) {
            mov_stack_offset_to_avx_reg(dst);
        }
        return {
            stack_.release_stack_offset(std::move(dst)),
            dst_loc,
            std::move(src),
            src_loc};
    }

    Emitter::Operand Emitter::get_operand(
        StackElemRef elem, LocationType loc, bool always_append_literal)
    {
        switch (loc) {
        case LocationType::StackOffset:
            return stack_offset_to_mem(*elem->stack_offset());
        case LocationType::GeneralReg:
            return general_reg_to_gpq256(*elem->general_reg());
        case LocationType::Literal:
            if (!always_append_literal &&
                is_literal_bounded(*elem->literal())) {
                return literal_to_imm256(*elem->literal());
            }
            else {
                auto lbl = append_literal(*elem->literal());
                return x86::qword_ptr(lbl);
            }
        case LocationType::AvxReg:
            return avx_reg_to_ymm(*elem->avx_reg());
        }

        std::unreachable();
    }

    template <
        Emitter::GeneralBinInstr<x86::Gp, x86::Gp> GG,
        Emitter::GeneralBinInstr<x86::Gp, x86::Mem> GM,
        Emitter::GeneralBinInstr<x86::Gp, asmjit::Imm> GI,
        Emitter::GeneralBinInstr<x86::Mem, x86::Gp> MG,
        Emitter::GeneralBinInstr<x86::Mem, asmjit::Imm> MI>
    void Emitter::general_bin_instr(
        StackElemRef dst, LocationType dst_loc, StackElemRef src,
        LocationType src_loc,
        std::function<bool(size_t, uint64_t)> is_no_operation)
    {
        auto dst_op = get_operand(dst, dst_loc);
        auto src_op = get_operand(src, src_loc);
        MONAD_COMPILER_DEBUG_ASSERT(!std::holds_alternative<x86::Ymm>(src_op));

        size_t instr_ix = 0;
        auto isnop = [&] -> std::function<bool(size_t, size_t)> {
            if (src->literal()) {
                return [&](size_t ins, size_t i) {
                    return is_no_operation(ins, src->literal()->value[i]);
                };
            }
            else {
                return [](size_t, size_t) { return false; };
            }
        }();

        if (std::holds_alternative<Gpq256>(dst_op)) {
            Gpq256 const &dst_gpq = std::get<Gpq256>(dst_op);
            std::visit(
                Cases{
                    [&](Gpq256 const &src_gpq) {
                        for (size_t i = 0; i < 4; ++i) {
                            if (!isnop(instr_ix, i)) {
                                (as_.*GG[instr_ix++])(dst_gpq[i], src_gpq[i]);
                            }
                        }
                    },
                    [&](x86::Mem const &src_mem) {
                        x86::Mem temp{src_mem};
                        for (size_t i = 0; i < 4; ++i) {
                            if (!isnop(instr_ix, i)) {
                                (as_.*GM[instr_ix++])(dst_gpq[i], temp);
                            }
                            temp.addOffset(8);
                        }
                    },
                    [&](Imm256 const &src_imm) {
                        for (size_t i = 0; i < 4; ++i) {
                            if (!isnop(instr_ix, i)) {
                                (as_.*GI[instr_ix++])(dst_gpq[i], src_imm[i]);
                            }
                        }
                    },
                    [](x86::Ymm const &) { std::unreachable(); },
                },
                src_op);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(
                std::holds_alternative<x86::Mem>(dst_op));
            MONAD_COMPILER_DEBUG_ASSERT(
                !std::holds_alternative<x86::Mem>(src_op));

            x86::Mem const &dst_mem = std::get<x86::Mem>(dst_op);
            std::visit(
                Cases{
                    [&](Gpq256 const &src_gpq) {
                        x86::Mem temp{dst_mem};
                        for (size_t i = 0; i < 4; ++i) {
                            if (!isnop(instr_ix, i)) {
                                (as_.*MG[instr_ix++])(temp, src_gpq[i]);
                            }
                            temp.addOffset(8);
                        }
                    },
                    [&](Imm256 const &src_imm) {
                        x86::Mem temp{dst_mem};
                        for (size_t i = 0; i < 4; ++i) {
                            if (!isnop(instr_ix, i)) {
                                (as_.*MI[instr_ix++])(temp, src_imm[i]);
                            }
                            temp.addOffset(8);
                        }
                    },
                    [](auto const &) { std::unreachable(); },
                },
                src_op);
        }
    }

    // Note that if dst_ix is null, then it is assumed that the unary avx
    // instruction will not mutate the destination register.
    template <typename... LiveSet>
    std::tuple<StackElemRef, StackElemRef, Emitter::LocationType>
    Emitter::get_una_arguments(
        StackElemRef dst, std::optional<int32_t> dst_ix,
        std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!dst->literal());
        RegReserv const dst_reserv{dst};
        if (!dst->avx_reg()) {
            if (dst->stack_offset()) {
                mov_stack_offset_to_avx_reg(dst);
            }
            else if (is_live(dst, live)) {
                mov_general_reg_to_avx_reg(dst);
            }
        }
        if (dst->avx_reg()) {
            if (!dst_ix) {
                return {dst, dst, LocationType::AvxReg};
            }
            if (!is_live(dst, live)) {
                auto n = stack_.release_avx_reg(std::move(dst));
                return {n, n, LocationType::AvxReg};
            }
            auto [n, _] = alloc_avx_reg();
            return {n, dst, LocationType::AvxReg};
        }
        MONAD_COMPILER_DEBUG_ASSERT(dst->general_reg() && !is_live(dst, live));
        auto n = stack_.release_general_reg(std::move(dst));
        return {n, n, LocationType::GeneralReg};
    }

    template <typename... LiveSet>
    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::prepare_avx_or_general_arguments_commutative(
        StackElemRef dst, StackElemRef src, std::tuple<LiveSet...> const &live)
    {
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if (dst.get() == src.get()) {
            if (dst->avx_reg()) {
                return {
                    std::move(dst),
                    LocationType::AvxReg,
                    std::move(src),
                    LocationType::AvxReg};
            }
            if (dst->general_reg() && !is_live(dst, live)) {
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::GeneralReg};
            }
            if (dst->stack_offset()) {
                mov_stack_offset_to_avx_reg(dst);
                return {
                    std::move(dst),
                    LocationType::AvxReg,
                    std::move(src),
                    LocationType::AvxReg};
            }
            if (dst->literal()) {
                mov_literal_to_avx_reg(dst);
                return {
                    std::move(dst),
                    LocationType::AvxReg,
                    std::move(src),
                    LocationType::AvxReg};
            }
            MONAD_COMPILER_DEBUG_ASSERT(dst->general_reg().has_value());
            mov_general_reg_to_avx_reg(dst);
            return {
                std::move(dst),
                LocationType::AvxReg,
                std::move(src),
                LocationType::AvxReg};
        }

        // We need to consider 15 cases for the pair (dst, src). Not 16, because
        // the case (literal, literal) is not possible.
        MONAD_COMPILER_DEBUG_ASSERT(
            !dst->literal().has_value() || !src->literal().has_value());

        using OptResult = std::optional<
            std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>>;

        OptResult result{};

        auto priority_1 = [](StackElemRef &d, StackElemRef &s) -> OptResult {
            if (d->avx_reg()) {
                if (s->avx_reg()) {
                    return OptResult(
                        {std::move(d),
                         LocationType::AvxReg,
                         std::move(s),
                         LocationType::AvxReg});
                }
                if (s->stack_offset()) {
                    return OptResult(
                        {std::move(d),
                         LocationType::AvxReg,
                         std::move(s),
                         LocationType::StackOffset});
                }
                if (s->literal()) {
                    return OptResult(
                        {std::move(d),
                         LocationType::AvxReg,
                         std::move(s),
                         LocationType::Literal});
                }
            }
            return std::nullopt;
        };

        // Case 1: (avx, avx)
        // Case 2: (avx, stack)
        // Case 3: (avx, literal)
        result = priority_1(dst, src);
        if (result.has_value()) {
            return *result;
        }
        // Case 4: (stack, avx)
        // Case 5: (literal, avx)
        result = priority_1(src, dst);
        if (result.has_value()) {
            return *result;
        }

        auto priority_2 =
            [&, this](StackElemRef &d, StackElemRef &s) -> OptResult {
            if (d->stack_offset()) {
                if (s->stack_offset()) {
                    if (is_live(s, live)) {
                        mov_stack_offset_to_avx_reg(s);
                        return priority_1(s, d);
                    }
                    mov_stack_offset_to_avx_reg(d);
                    return priority_1(d, s);
                }
                if (s->literal()) {
                    if (is_live(s, live)) {
                        mov_literal_to_avx_reg(s);
                        return priority_1(s, d);
                    }
                    mov_stack_offset_to_avx_reg(d);
                    return priority_1(d, s);
                }
            }
            return std::nullopt;
        };

        // Case 6: (stack, stack)
        // Case 7: (stack, literal)
        result = priority_2(dst, src);
        if (result.has_value()) {
            return *result;
        }
        // Case 8: (literal, stack)
        result = priority_2(src, dst);
        if (result.has_value()) {
            return *result;
        }

        auto priority_3 =
            [&, this](StackElemRef &d, StackElemRef &s) -> OptResult {
            if (!d->general_reg()) {
                return std::nullopt;
            }
            if (is_live(d, live) && !d->literal() && !d->stack_offset() &&
                !d->avx_reg()) {
                return std::nullopt;
            }
            if (s->general_reg()) {
                return OptResult(
                    {std::move(d),
                     LocationType::GeneralReg,
                     std::move(s),
                     LocationType::GeneralReg});
            }
            if (s->stack_offset()) {
                return OptResult(
                    {std::move(d),
                     LocationType::GeneralReg,
                     std::move(s),
                     LocationType::StackOffset});
            }
            if (s->literal()) {
                return OptResult(
                    {std::move(d),
                     LocationType::GeneralReg,
                     std::move(s),
                     LocationType::Literal});
            }
            return std::nullopt;
        };

        // Case 9 (conditional): (general, general)
        // Case 10 (conditional): (general, stack)
        // Case 11 (conditional): (general, literal)
        result = priority_3(dst, src);
        if (result.has_value()) {
            return *result;
        }
        // Case 12 (conditional): (stack, general)
        // Case 13 (conditional): (literal, general)
        result = priority_3(src, dst);
        if (result.has_value()) {
            return *result;
        }

        auto priority_4 =
            [this](StackElemRef &d, StackElemRef &s) -> OptResult {
            if (d->avx_reg() && s->general_reg()) {
                mov_general_reg_to_stack_offset(s);
                return OptResult(
                    {std::move(d),
                     LocationType::AvxReg,
                     std::move(s),
                     LocationType::StackOffset});
            }
            return std::nullopt;
        };

        // Case 14: (avx, general)
        // Case 15: (general, avx)
        if (is_live(src, live)) {
            result = priority_4(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (is_live(dst, live)) {
            result = priority_4(src, dst);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!is_live(src, live)) {
            result = priority_4(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!is_live(dst, live)) {
            result = priority_4(src, dst);
            if (result.has_value()) {
                return *result;
            }
        }

        auto priority_5 =
            [this](StackElemRef &d, StackElemRef &s) -> OptResult {
            if (!s->general_reg()) {
                return std::nullopt;
            }
            if (d->stack_offset()) {
                mov_general_reg_to_avx_reg(s);
                return OptResult(
                    {std::move(s),
                     LocationType::AvxReg,
                     std::move(d),
                     LocationType::StackOffset});
            }
            if (d->literal()) {
                mov_general_reg_to_avx_reg(s);
                return OptResult(
                    {std::move(s),
                     LocationType::AvxReg,
                     std::move(d),
                     LocationType::Literal});
            }
            return std::nullopt;
        };

        // Case 12 (unconditional): (stack, general)
        // Case 10 (unconditional): (general, stack)
        // Case 11 (unconditional): (general, literal)
        // Case 13 (unconditional): (literal, general)
        if (is_live(src, live)) {
            result = priority_5(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (is_live(dst, live)) {
            result = priority_5(src, dst);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!is_live(src, live)) {
            result = priority_5(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!is_live(dst, live)) {
            result = priority_5(src, dst);
            if (result.has_value()) {
                return *result;
            }
        }

        // Case 9 (unconditional): (general, general)
        MONAD_COMPILER_DEBUG_ASSERT(dst->general_reg() && src->general_reg());
        mov_general_reg_to_stack_offset(dst);
        return {
            std::move(dst),
            LocationType::GeneralReg,
            std::move(src),
            LocationType::GeneralReg};
    }

    template <typename... LiveSet>
    std::tuple<
        StackElemRef, StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::get_avx_or_general_arguments_commutative(
        StackElemRef dst_in, StackElemRef src_in,
        std::tuple<LiveSet...> const &live)
    {
        auto [dst, dst_loc, src, src_loc] =
            prepare_avx_or_general_arguments_commutative(
                std::move(dst_in), std::move(src_in), live);
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if (dst_loc == LocationType::GeneralReg) {
            MONAD_COMPILER_DEBUG_ASSERT(
                !is_live(dst, live) || dst->stack_offset().has_value() ||
                dst->literal().has_value() || dst->avx_reg().has_value());
            auto new_dst = stack_.release_general_reg(dst);
            if (dst == src) {
                return {new_dst, new_dst, dst_loc, new_dst, src_loc};
            }
            else {
                return {new_dst, new_dst, dst_loc, std::move(src), src_loc};
            }
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(dst_loc == LocationType::AvxReg);
            if (is_live(dst, live)) {
                if (!is_live(src, live) && src_loc == LocationType::AvxReg) {
                    auto n = stack_.release_avx_reg(src);
                    return {n, std::move(dst), dst_loc, n, src_loc};
                }
                else {
                    auto [n, _] = alloc_avx_reg();
                    return {
                        n, std::move(dst), dst_loc, std::move(src), src_loc};
                }
            }
            auto n = stack_.release_avx_reg(dst);
            return {n, n, dst_loc, std::move(src), src_loc};
        }
    }

    template <
        Emitter::GeneralBinInstr<x86::Gp, x86::Gp> GG,
        Emitter::GeneralBinInstr<x86::Gp, x86::Mem> GM,
        Emitter::GeneralBinInstr<x86::Gp, asmjit::Imm> GI,
        Emitter::GeneralBinInstr<x86::Mem, x86::Gp> MG,
        Emitter::GeneralBinInstr<x86::Mem, asmjit::Imm> MI,
        Emitter::AvxBinInstr<x86::Vec> VV, Emitter::AvxBinInstr<x86::Mem> VM>
    void Emitter::avx_or_general_bin_instr(
        StackElemRef dst, StackElemRef left, LocationType left_loc,
        StackElemRef right, LocationType right_loc,
        std::function<bool(size_t, uint64_t)> is_no_operation)
    {
        if (left_loc == LocationType::GeneralReg) {
            general_bin_instr<GG, GM, GI, MG, MI>(
                std::move(left),
                left_loc,
                std::move(right),
                right_loc,
                std::move(is_no_operation));
            return;
        }
        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        MONAD_COMPILER_DEBUG_ASSERT(dst->avx_reg().has_value());
        MONAD_COMPILER_DEBUG_ASSERT(std::holds_alternative<x86::Ymm>(left_op));
        if (std::holds_alternative<x86::Ymm>(right_op)) {
            (as_.*VV)(
                avx_reg_to_ymm(*dst->avx_reg()),
                std::get<x86::Ymm>(left_op),
                std::get<x86::Ymm>(right_op));
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(
                std::holds_alternative<x86::Mem>(right_op));
            (as_.*VM)(
                avx_reg_to_ymm(*dst->avx_reg()),
                std::get<x86::Ymm>(left_op),
                std::get<x86::Mem>(right_op));
        }
    }

    void Emitter::negate_gpq256(Gpq256 const &gpq)
    {
        for (auto const &r : gpq) {
            as_.not_(r);
        }
        as_.add(gpq[0], 1);
        as_.adc(gpq[1], 0);
        as_.adc(gpq[2], 0);
        as_.adc(gpq[3], 0);
    }

    StackElemRef Emitter::negate_by_sub(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!elem->literal().has_value());

        GeneralRegReserv const reserv{elem};

        auto [dst, dst_reserv] = alloc_general_reg();
        mov_literal_to_gpq256(
            Literal{0}, general_reg_to_gpq256(*dst->general_reg()));

        auto const &d = general_reg_to_gpq256(*dst->general_reg());
        if (elem->general_reg()) {
            auto const &e = general_reg_to_gpq256(*elem->general_reg());
            as_.sub(d[0], e[0]);
            for (size_t i = 1; i < 4; ++i) {
                as_.sbb(d[i], e[i]);
            }
        }
        else {
            if (!elem->stack_offset()) {
                MONAD_COMPILER_DEBUG_ASSERT(elem->avx_reg().has_value());
                mov_avx_reg_to_stack_offset(elem);
            }
            auto m = stack_offset_to_mem(*elem->stack_offset());
            as_.sub(d[0], m);
            for (size_t i = 1; i < 4; ++i) {
                m.addOffset(8);
                as_.sbb(d[i], m);
            }
        }

        return dst;
    }

    template <typename... LiveSet>
    StackElemRef
    Emitter::negate(StackElemRef elem, std::tuple<LiveSet...> const &live)
    {
        if (elem->literal()) {
            auto const &v = elem->literal()->value;
            return stack_.alloc_literal({-v});
        }
        discharge_deferred_comparison();
        if (!elem->general_reg() ||
            (is_live(elem, live) && !elem->stack_offset() &&
             !elem->avx_reg())) {
            return negate_by_sub(std::move(elem));
        }
        auto dst = stack_.release_general_reg(std::move(elem));
        negate_gpq256(general_reg_to_gpq256(*dst->general_reg()));
        return dst;
    }

    // Scrambles rdx
    // Does not update eflags
    template <typename LeftOpType>
    void
    Emitter::mulx(x86::Gpq dst1, x86::Gpq dst2, LeftOpType left, x86::Gpq right)
    {
        as_.mov(x86::rdx, left);
        as_.mulx(dst1, dst2, right);
    }

    template <bool Is32Bit, typename LeftOpType>
    void Emitter::imul_by_gpq(x86::Gpq dst, LeftOpType left, x86::Gpq right)
    {
        as_.mov(dst, right);
        if constexpr (Is32Bit) {
            if constexpr (std::is_same_v<LeftOpType, x86::Gpq>) {
                as_.imul(dst.r32(), left.r32());
            }
            else {
                as_.imul(dst.r32(), left);
            }
        }
        else {
            as_.imul(dst, left);
        }
    }

    template <bool Is32Bit, typename LeftOpType>
    void Emitter::imul_by_int32(x86::Gpq dst, LeftOpType left, int32_t right)
    {
        if constexpr (Is32Bit) {
            if constexpr (std::is_same_v<LeftOpType, x86::Gpq>) {
                as_.imul(dst.r32(), left.r32(), right);
            }
            else {
                as_.imul(dst.r32(), left, right);
            }
        }
        else {
            as_.imul(dst, left, right);
        }
    }

    template <bool Is32Bit, typename LeftOpType>
    void Emitter::imul_by_rax_or_int32(
        asmjit::x86::Gpq dst, LeftOpType left, std::optional<int32_t> i)
    {
        if (i) {
            imul_by_int32<Is32Bit>(dst, left, *i);
        }
        else {
            imul_by_gpq<Is32Bit>(dst, left, x86::rax);
        }
    }

    void Emitter::mul_with_bit_size_by_rax(
        size_t bit_size, x86::Gpq const *dst, Operand const &left,
        std::optional<int32_t> value_of_rax)
    {
        if ((bit_size & 63) && (bit_size & 63) <= 32) {
            mul_with_bit_size_and_has_32_bit_by_rax<true>(
                bit_size, dst, left, value_of_rax);
        }
        else {
            mul_with_bit_size_and_has_32_bit_by_rax<false>(
                bit_size, dst, left, value_of_rax);
        }
    }

    // Scrambles rdx
    template <bool Has32Bit>
    void Emitter::mul_with_bit_size_and_has_32_bit_by_rax(
        size_t bit_size, x86::Gpq const *dst, Operand const &left,
        std::optional<int32_t> value_of_rax)
    {
        MONAD_COMPILER_DEBUG_ASSERT(bit_size > 0 && bit_size <= 256);

        constexpr auto right = x86::rax;

        auto const last_ix = div64_ceil(bit_size) - 1;

        auto next_dst_pair = [&](size_t i) -> std::pair<x86::Gpq, x86::Gpq> {
            auto dst1 = i == last_ix - 1 ? x86::rax : dst[i + 1];
            auto dst2 = i == 0 ? dst[0] : x86::rdx;
            return {dst1, dst2};
        };

        auto post_add = [&](size_t i) {
            if (last_ix == 1) {
                if constexpr (Has32Bit) {
                    as_.add(dst[1].r32(), x86::eax);
                }
                else {
                    as_.add(dst[1], x86::rax);
                }
            }
            else if (i > 0) {
                if (i == 1) {
                    as_.add(dst[1], x86::rdx);
                }
                else {
                    as_.adc(dst[i], x86::rdx);
                }
                if (i == last_ix - 1) {
                    if constexpr (Has32Bit) {
                        as_.adc(dst[last_ix].r32(), x86::eax);
                    }
                    else {
                        as_.adc(dst[last_ix], x86::rax);
                    }
                }
            }
        };

        if (std::holds_alternative<Gpq256>(left)) {
            auto const &lgpq = std::get<Gpq256>(left);
            imul_by_rax_or_int32<Has32Bit>(
                dst[last_ix], lgpq[last_ix], value_of_rax);
            for (size_t i = 0; i < last_ix; ++i) {
                auto [dst1, dst2] = next_dst_pair(i);
                mulx(dst1, dst2, lgpq[i], right);
                post_add(i);
            }
        }
        else {
            MONAD_COMPILER_ASSERT(std::holds_alternative<x86::Mem>(left));
            auto mem = std::get<x86::Mem>(left);
            mem.addOffset(static_cast<int64_t>(last_ix) * 8);
            imul_by_rax_or_int32<Has32Bit>(dst[last_ix], mem, value_of_rax);
            mem.addOffset(-(static_cast<int64_t>(last_ix) * 8));
            for (size_t i = 0; i < last_ix; ++i) {
                auto [dst1, dst2] = next_dst_pair(i);
                mulx(dst1, dst2, mem, right);
                post_add(i);
                mem.addOffset(8);
            }
        }
    }

    Emitter::MulEmitter::MulEmitter(
        size_t bit_size, Emitter &em, Operand const &left,
        RightMulArg const &right, x86::Gpq const *dst, x86::Gpq const *tmp)
        : bit_size_{bit_size}
        , em_{em}
        , left_{left}
        , right_{right}
        , dst_{dst}
        , tmp_{tmp}
        , is_dst_initialized_{}
    {
    }

    void Emitter::MulEmitter::init_mul_dst(size_t sub_size, x86::Gpq *mul_dst)
    {
        size_t const N = div64_ceil(sub_size);
        if (is_dst_initialized_) {
            for (size_t i = 0; i < N; ++i) {
                mul_dst[i] = tmp_[i];
            }
        }
        else {
            size_t const c = div64_ceil(bit_size_);
            for (size_t i = c - N, n = 0; i < c; ++i) {
                mul_dst[n++] = dst_[i];
            }
        }
    }

    template <bool Has32Bit>
    void
    Emitter::MulEmitter::mul_sequence(size_t sub_size, x86::Gpq const *mul_dst)
    {
        size_t const word_count = div64_ceil(bit_size_);
        size_t const N = div64_ceil(sub_size);
        if (std::holds_alternative<uint256_t>(right_) &&
            std::get<uint256_t>(right_)[word_count - N] == 1) {
            if (std::holds_alternative<Gpq256>(left_)) {
                auto const &lgpq = std::get<Gpq256>(left_);
                size_t i = 0;
                for (; i < N - 1; ++i) {
                    em_.as_.mov(mul_dst[i], lgpq[i]);
                }
                if constexpr (Has32Bit) {
                    em_.as_.mov(mul_dst[i].r32(), lgpq[i].r32());
                }
                else {
                    em_.as_.mov(mul_dst[i], lgpq[i]);
                }
            }
            else {
                MONAD_COMPILER_DEBUG_ASSERT(
                    std::holds_alternative<x86::Mem>(left_));
                auto lmem = std::get<x86::Mem>(left_);
                size_t i = 0;
                for (; i < N - 1; ++i) {
                    em_.as_.mov(mul_dst[i], lmem);
                    lmem.addOffset(8);
                }
                if constexpr (Has32Bit) {
                    em_.as_.mov(mul_dst[i].r32(), lmem);
                }
                else {
                    em_.as_.mov(mul_dst[i], lmem);
                }
            }
        }
        else if (N > 1) {
            auto known_value = std::visit(
                Cases{
                    [&](uint256_t const &r) {
                        auto x = r[word_count - N];
                        em_.as_.mov(x86::rax, x);
                        if (!is_uint64_bounded(x)) {
                            return std::optional<int32_t>{};
                        }
                        int32_t y;
                        std::memcpy(&y, &x, sizeof(int32_t));
                        return std::optional{y};
                    },
                    [&](Gpq256 const &r) {
                        em_.as_.mov(x86::rax, r[word_count - N]);
                        return std::optional<int32_t>{};
                    },
                    [&](x86::Mem r) {
                        r.addOffset(static_cast<int64_t>((word_count - N) * 8));
                        em_.as_.mov(x86::rax, r);
                        return std::optional<int32_t>{};
                    },
                },
                right_);
            em_.mul_with_bit_size_by_rax(sub_size, mul_dst, left_, known_value);
        }
        else if (std::holds_alternative<Gpq256>(left_)) {
            auto const &lgpq = std::get<Gpq256>(left_);
            std::visit(
                Cases{
                    [&](uint256_t const &r) {
                        auto x = r[word_count - N];
                        if constexpr (Has32Bit) {
                            em_.as_.imul(mul_dst[0].r32(), lgpq[0].r32(), x);
                        }
                        else if (is_uint64_bounded(x)) {
                            em_.as_.imul(mul_dst[0], lgpq[0], x);
                        }
                        else {
                            em_.as_.mov(mul_dst[0], x);
                            em_.as_.imul(mul_dst[0], lgpq[0]);
                        }
                    },
                    [&](Gpq256 const &r) {
                        if constexpr (Has32Bit) {
                            em_.as_.mov(
                                mul_dst[0].r32(), r[word_count - N].r32());
                            em_.as_.imul(mul_dst[0].r32(), lgpq[0].r32());
                        }
                        else {
                            em_.as_.mov(mul_dst[0], r[word_count - N]);
                            em_.as_.imul(mul_dst[0], lgpq[0]);
                        }
                    },
                    [&](x86::Mem r) {
                        r.addOffset(static_cast<int64_t>((word_count - N) * 8));
                        if constexpr (Has32Bit) {
                            em_.as_.mov(mul_dst[0].r32(), r);
                            em_.as_.imul(mul_dst[0].r32(), lgpq[0].r32());
                        }
                        else {
                            em_.as_.mov(mul_dst[0], r);
                            em_.as_.imul(mul_dst[0], lgpq[0]);
                        }
                    },
                },
                right_);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(
                std::holds_alternative<x86::Mem>(left_));
            auto const &lmem = std::get<x86::Mem>(left_);
            std::visit(
                Cases{
                    [&](uint256_t const &r) {
                        auto x = r[word_count - N];
                        if constexpr (Has32Bit) {
                            em_.as_.imul(mul_dst[0].r32(), lmem, x);
                        }
                        else if (is_uint64_bounded(x)) {
                            em_.as_.imul(mul_dst[0], lmem, x);
                        }
                        else {
                            em_.as_.mov(mul_dst[0], x);
                            em_.as_.imul(mul_dst[0], lmem);
                        }
                    },
                    [&](Gpq256 const &r) {
                        if constexpr (Has32Bit) {
                            em_.as_.mov(
                                mul_dst[0].r32(), r[word_count - N].r32());
                            em_.as_.imul(mul_dst[0].r32(), lmem);
                        }
                        else {
                            em_.as_.mov(mul_dst[0], r[word_count - N]);
                            em_.as_.imul(mul_dst[0], lmem);
                        }
                    },
                    [&](x86::Mem r) {
                        r.addOffset(static_cast<int64_t>((word_count - N) * 8));
                        if constexpr (Has32Bit) {
                            em_.as_.mov(mul_dst[0].r32(), r);
                            em_.as_.imul(mul_dst[0].r32(), lmem);
                        }
                        else {
                            em_.as_.mov(mul_dst[0], r);
                            em_.as_.imul(mul_dst[0], lmem);
                        }
                    },
                },
                right_);
        }
    }

    template <bool Has32Bit>
    void
    Emitter::MulEmitter::update_dst(size_t sub_size, x86::Gpq const *mul_dst)
    {
        if (is_dst_initialized_) {
            size_t const word_count = div64_ceil(bit_size_);
            size_t i = word_count - div64_ceil(sub_size);
            size_t j = 0;
            if constexpr (Has32Bit) {
                if (i == word_count - 1) {
                    em_.as_.add(dst_[i++].r32(), mul_dst[j++].r32());
                }
                else {
                    em_.as_.add(dst_[i++], mul_dst[j++]);
                }
                while (i < word_count) {
                    if (i == word_count - 1) {
                        em_.as_.adc(dst_[i++].r32(), mul_dst[j++].r32());
                    }
                    else {
                        em_.as_.adc(dst_[i++], mul_dst[j++]);
                    }
                }
            }
            else {
                em_.as_.add(dst_[i++], mul_dst[j++]);
                while (i < word_count) {
                    em_.as_.adc(dst_[i++], mul_dst[j++]);
                }
            }
        }
        else {
            is_dst_initialized_ = true;
        }
    }

    template <bool Has32Bit>
    void Emitter::MulEmitter::compose(size_t sub_size, x86::Gpq *mul_dst)
    {
        size_t const i = div64_ceil(bit_size_) - div64_ceil(sub_size);
        if (!std::holds_alternative<uint256_t>(right_) ||
            std::get<uint256_t>(right_)[i] != 0) {
            init_mul_dst(sub_size, mul_dst);
            mul_sequence<Has32Bit>(sub_size, mul_dst);
            update_dst<Has32Bit>(sub_size, mul_dst);
        }
        else if (!is_dst_initialized_) {
            em_.as_.xor_(dst_[i], dst_[i]);
        }
    }

    template <bool Has32Bit>
    void Emitter::MulEmitter::emit_loop()
    {
        x86::Gpq mul_dst[4];
        auto sub_size = bit_size_;
        while (sub_size > 64) {
            compose<Has32Bit>(sub_size, mul_dst);
            sub_size -= 64;
        }
        compose<Has32Bit>(sub_size, mul_dst);
    }

    void Emitter::MulEmitter::emit()
    {
        if ((bit_size_ & 63) && (bit_size_ & 63) <= 32) {
            emit_loop<true>();
        }
        else {
            emit_loop<false>();
        }
    }

    // If right is `Gpq256`, then make sure the general register is
    // reserved with `GeneralRegReserv`.
    template <typename... LiveSet>
    StackElemRef Emitter::mul_with_bit_size(
        size_t bit_size, StackElemRef left, MulEmitter::RightMulArg right,
        std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(bit_size > 0 && bit_size <= 256);
        MONAD_COMPILER_DEBUG_ASSERT(
            rdx_general_reg_index == 1 || rdx_general_reg_index == 2);

        size_t const dst_word_count = div64_ceil(bit_size);

        MONAD_COMPILER_DEBUG_ASSERT(!left->literal().has_value());

        discharge_deferred_comparison();

        size_t required_reg_count = 0;
        bool needs_mulx = true;
        for (size_t i = 0; i < dst_word_count; ++i) {
            if (!std::holds_alternative<uint256_t>(right) ||
                std::get<uint256_t>(right)[i] != 0) {
                if (required_reg_count == 0) {
                    required_reg_count = dst_word_count;
                    needs_mulx = i != dst_word_count - 1;
                }
                else {
                    required_reg_count += (dst_word_count - i);
                    break;
                }
            }
        }

        if (required_reg_count == 0) {
            return stack_.alloc_literal({0});
        }

        MONAD_COMPILER_DEBUG_ASSERT(
            required_reg_count >= dst_word_count && required_reg_count < 8);

        GeneralRegReserv const left_reserv{left};
        if (required_reg_count > dst_word_count) {
            if (!left->general_reg()) {
                mov_stack_elem_to_general_reg(left);
            }
        }
        else {
            if (!left->general_reg() && !left->stack_offset()) {
                MONAD_COMPILER_DEBUG_ASSERT(left->avx_reg().has_value());
                mov_avx_reg_to_stack_offset(left);
            }
        }

        auto [dst, dst_reserv] = alloc_general_reg();

        auto tmp = dst;
        auto tmp_reserv = dst_reserv;
        if (required_reg_count > 4) {
            auto [t, r] = alloc_general_reg();
            tmp = t;
            tmp_reserv = r;
        }

        auto spill_elem = tmp;
        auto spill_elem_reserv = tmp_reserv;
        std::optional<x86::Gpq> spill_gpq;
        if (needs_mulx && stack_.has_free_general_reg()) {
            auto [s, r] = alloc_general_reg();
            spill_elem = s;
            spill_elem_reserv = r;
            auto const &gpq = general_reg_to_gpq256(*spill_elem->general_reg());
            spill_gpq = gpq[rdx_general_reg_index];
        }

        bool preserve_dst_rdx = false;
        bool preserve_left_rdx = false;
        bool preserve_right_rdx = false;
        bool preserve_stack_rdx = false;

        if (needs_mulx) {
            auto &dst_gpq = general_reg_to_gpq256(*dst->general_reg());
            auto &tmp_gpq = general_reg_to_gpq256(*tmp->general_reg());
            if (dst_gpq[rdx_general_reg_index] == x86::rdx) {
                MONAD_COMPILER_DEBUG_ASSERT(
                    *dst->general_reg() == rdx_general_reg);
                preserve_dst_rdx = true;
            }
            if (preserve_dst_rdx) {
                if (tmp != dst) {
                    std::swap(tmp, dst);
                    preserve_dst_rdx = false;
                }
                else {
                    if (spill_gpq) {
                        dst_gpq[rdx_general_reg_index] = *spill_gpq;
                    }
                    else {
                        as_.push(reg_context);
                        dst_gpq[rdx_general_reg_index] = reg_context;
                    }
                }
            }
            else {
                if (left->general_reg()) {
                    auto &left_gpq =
                        general_reg_to_gpq256(*left->general_reg());
                    if (left_gpq[rdx_general_reg_index] == x86::rdx) {
                        MONAD_COMPILER_DEBUG_ASSERT(
                            *left->general_reg() == rdx_general_reg);
                        if (tmp != dst) {
                            spill_gpq = tmp_gpq[rdx_general_reg_index];
                        }
                        preserve_left_rdx = true;
                        if (spill_gpq) {
                            as_.mov(*spill_gpq, x86::rdx);
                            left_gpq[rdx_general_reg_index] = *spill_gpq;
                        }
                        else {
                            as_.push(reg_context);
                            as_.mov(reg_context, x86::rdx);
                            left_gpq[rdx_general_reg_index] = reg_context;
                        }
                    }
                }
                if (std::holds_alternative<Gpq256>(right) &&
                    dst_word_count > rdx_general_reg_index) {
                    auto &right_gpq = std::get<Gpq256>(right);
                    if (right_gpq[rdx_general_reg_index] == x86::rdx) {
                        if (tmp != dst) {
                            spill_gpq = tmp_gpq[rdx_general_reg_index];
                        }
                        preserve_right_rdx = true;
                        if (spill_gpq) {
                            as_.mov(*spill_gpq, x86::rdx);
                            right_gpq[rdx_general_reg_index] = *spill_gpq;
                        }
                        else {
                            as_.push(reg_context);
                            as_.mov(reg_context, x86::rdx);
                            right_gpq[rdx_general_reg_index] = reg_context;
                        }
                    }
                }
                if (!preserve_left_rdx && !preserve_right_rdx &&
                    is_live(rdx_general_reg, live)) {
                    auto const &q = general_reg_to_gpq256(rdx_general_reg);
                    MONAD_COMPILER_DEBUG_ASSERT(
                        q[rdx_general_reg_index] == x86::rdx);
                    preserve_stack_rdx = true;
                    if (spill_gpq) {
                        as_.mov(*spill_gpq, x86::rdx);
                    }
                    else {
                        as_.push(x86::rdx);
                    }
                }
            }
        }

        auto &dst_gpq = general_reg_to_gpq256(*dst->general_reg());
        auto left_op =
            left->general_reg()
                ? Operand{general_reg_to_gpq256(*left->general_reg())}
                : Operand{stack_offset_to_mem(*left->stack_offset())};
        MONAD_COMPILER_DEBUG_ASSERT(dst_word_count <= 4);
        x86::Gpq emit_tmp[3];
        if (tmp != dst) {
            auto &tmp_gpq = general_reg_to_gpq256(*tmp->general_reg());
            for (size_t i = 0, n = 0; n < dst_word_count - 1; ++i) {
                if (i != rdx_general_reg_index) {
                    emit_tmp[n++] = tmp_gpq[i];
                }
            }
        }
        else {
            for (size_t i = 0, n = dst_word_count;
                 n < 4 && i < dst_word_count - 1;
                 ++i) {
                emit_tmp[i] = dst_gpq[n++];
            }
        }

        MulEmitter{bit_size, *this, left_op, right, dst_gpq.data(), emit_tmp}
            .emit();

        if (bit_size & 31) {
            auto mask = (uint64_t{1} << (bit_size & 63)) - 1;
            if (std::bit_width(mask) <= 32) {
                as_.and_(dst_gpq[dst_word_count - 1].r32(), mask);
            }
            else {
                as_.mov(x86::rax, mask);
                as_.and_(dst_gpq[dst_word_count - 1], x86::rax);
            }
        }
        for (size_t i = dst_word_count; i < 4; ++i) {
            as_.xor_(dst_gpq[i], dst_gpq[i]);
        }

        if (preserve_stack_rdx) {
            if (spill_gpq) {
                as_.mov(x86::rdx, *spill_gpq);
            }
            else {
                as_.pop(x86::rdx);
            }
        }
        else if (preserve_dst_rdx) {
            if (spill_gpq) {
                as_.mov(x86::rdx, *spill_gpq);
                dst_gpq[rdx_general_reg_index] = x86::rdx;
            }
            else {
                as_.mov(x86::rdx, reg_context);
                dst_gpq[rdx_general_reg_index] = x86::rdx;
                as_.pop(reg_context);
            }
        }
        else if (preserve_left_rdx) {
            auto &left_gpq = general_reg_to_gpq256(*left->general_reg());
            if (spill_gpq) {
                as_.mov(x86::rdx, *spill_gpq);
                left_gpq[rdx_general_reg_index] = x86::rdx;
            }
            else {
                as_.mov(x86::rdx, reg_context);
                left_gpq[rdx_general_reg_index] = x86::rdx;
                as_.pop(reg_context);
            }
        }
        else if (preserve_right_rdx) {
            if (spill_gpq) {
                as_.mov(x86::rdx, *spill_gpq);
            }
            else {
                as_.mov(x86::rdx, reg_context);
                as_.pop(reg_context);
            }
        }

        return dst;
    }

    bool Emitter::mul_optimized()
    {
        auto a_elem = stack_.get(stack_.top_index());
        auto b_elem = stack_.get(stack_.top_index() - 1);

        if (b_elem->literal()) {
            if (a_elem->literal()) {
                auto const &a = a_elem->literal()->value;
                auto const &b = b_elem->literal()->value;
                stack_.pop();
                stack_.pop();
                stack_.push_literal(a * b);
                return true;
            }
            else {
                std::swap(a_elem, b_elem);
            }
        }
        else if (!a_elem->literal()) {
            return false;
        }

        auto a = std::move(a_elem)->literal()->value;
        if (a == 0) {
            stack_.pop();
            stack_.pop();
            stack_.push_literal(0);
            return true;
        }

        auto a_shift = a;
        if (a[3] & (static_cast<uint64_t>(1) << 63)) {
            a_shift = -a;
        }

        if (monad::utils::popcount(a_shift) == 1) {
            stack_.pop();
            stack_.pop();
            auto x = shift_by_literal<ShiftType::SHL>(
                monad::utils::countr_zero(a_shift), std::move(b_elem), {});
            if (a_shift[3] != a[3]) {
                // The shift was negated. Negate result for correct sign:
                stack_.push(negate(std::move(x), {}));
            }
            else {
                stack_.push(std::move(x));
            }
            return true;
        }
        else if (!a[0] || !a[1] || !a[2] || !a[3]) {
            // If one of the qwords in `a` is zero, then we will inline
            // the multiplication. This will save at least one x86
            // multiplication instruction.
            stack_.pop();
            stack_.pop();
            stack_.push(mul_with_bit_size(256, std::move(b_elem), a, {}));
            return true;
        }

        return false;
    }

    // Discharge through `shift_by_literal`.
    // Note that this function assumes that there is an available
    // stack offset in the stack. This is the case when calling
    // from `div_optimized`, because lifetime of the divisor has
    // ended before calling this function.
    template <typename... LiveSet>
    StackElemRef Emitter::sdiv_by_sar(
        StackElemRef elem, uint256_t const &shift_in,
        std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!elem->literal().has_value());
        MONAD_COMPILER_DEBUG_ASSERT(shift_in <= 255);

        auto shift = static_cast<uint64_t>(shift_in);

        if (shift == 0) {
            return elem;
        }

        size_t index = 3;
        for (auto c = 256 - shift;;) {
            if (c <= 64) {
                break;
            }
            c -= 64;
            --index;
        }
        auto mask = (static_cast<uint64_t>(1) << (shift & 63)) - 1;

        StackElemRef sh;
        {
            GeneralRegReserv const elem_reserv{elem};
            sh = shift_by_literal<ShiftType::SAR>(
                shift, elem, std::tuple_cat(std::make_tuple(elem), live));
        }

        GeneralRegReserv const sh_reserv{sh};

        if (!elem->general_reg() && stack_.has_free_general_reg()) {
            mov_stack_elem_to_general_reg(elem);
        }

        if (elem->general_reg()) {
            auto const &gpq = general_reg_to_gpq256(*elem->general_reg());
            if (mask != 0) {
                as_.mov(x86::rax, mask);
                as_.and_(x86::rax, gpq[index]);
            }
            else {
                as_.xor_(x86::rax, x86::rax);
            }
            while (index--) {
                as_.or_(x86::rax, gpq[index]);
            }
            as_.setnz(x86::al);

            auto const &lbl =
                append_literal(Literal{static_cast<uint64_t>(1) << 63});
            as_.test(x86::qword_ptr(lbl), gpq[3]);
            as_.setnz(x86::ah);

            as_.and_(x86::al, x86::ah);
            as_.movzx(x86::eax, x86::al);
        }
        else {
            if (!elem->stack_offset()) {
                MONAD_COMPILER_DEBUG_ASSERT(elem->avx_reg().has_value());
                mov_avx_reg_to_stack_offset(elem);
            }
            MONAD_COMPILER_DEBUG_ASSERT(elem->stack_offset().has_value());
            auto mem = stack_offset_to_mem(*elem->stack_offset());
            mem.addOffset(24);
            as_.mov(x86::rax, static_cast<uint64_t>(1) << 63);
            as_.test(mem, x86::rax);
            as_.setnz(x86::byte_ptr(x86::rsp, -1));

            MONAD_COMPILER_DEBUG_ASSERT(index <= 3);
            mem.addOffset(static_cast<int64_t>(index) * 8 - 24);
            as_.mov(x86::rax, mask);
            as_.and_(x86::rax, mem);
            while (index--) {
                mem.addOffset(-8);
                as_.or_(x86::rax, mem);
            }
            as_.setnz(x86::al);

            as_.and_(x86::al, x86::byte_ptr(x86::rsp, -1));
            as_.movzx(x86::eax, x86::al);
        }

        elem.reset(); // Release registers and stack offset.

        MONAD_COMPILER_DEBUG_ASSERT(!sh->literal().has_value());

        StackElemRef dst;
        if (is_live(sh, live)) {
            if (sh->general_reg() && (sh->stack_offset() || sh->avx_reg())) {
                dst = stack_.release_general_reg(sh);
            }
            else if (sh->stack_offset() && sh->avx_reg()) {
                dst = stack_.release_stack_offset(sh);
            }
            else {
                if (sh->general_reg() || sh->stack_offset()) {
                    auto [r, _] = alloc_general_reg();
                    dst = std::move(r);
                    mov_stack_elem_to_gpq256(
                        sh, general_reg_to_gpq256(*dst->general_reg()));
                }
                else {
                    mov_avx_reg_to_stack_offset(sh);
                    dst = stack_.release_stack_offset(sh);
                }
            }
        }
        else if (sh->general_reg()) {
            dst = stack_.release_general_reg(sh);
        }
        else if (sh->stack_offset()) {
            dst = stack_.release_stack_offset(sh);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(sh->avx_reg().has_value());
            mov_avx_reg_to_stack_offset(sh);
            dst = stack_.release_stack_offset(sh);
        }

        if (dst->general_reg()) {
            auto const &gpq = general_reg_to_gpq256(*dst->general_reg());
            as_.add(gpq[0], x86::rax);
            for (size_t i = 1; i < 4; ++i) {
                as_.adc(gpq[i], 0);
            }
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(dst->stack_offset().has_value());
            auto mem = stack_offset_to_mem(*dst->stack_offset());
            as_.add(mem, x86::rax);
            for (size_t i = 1; i < 4; ++i) {
                mem.addOffset(8);
                as_.adc(mem, 0);
            }
        }

        return dst;
    }

    template <bool is_sdiv>
    bool Emitter::div_optimized()
    {
        auto const a_elem = stack_.get(stack_.top_index());
        auto const b_elem = stack_.get(stack_.top_index() - 1);

        if (a_elem->literal()) {
            auto const &a = a_elem->literal()->value;
            if (a == 0) {
                stack_.pop();
                stack_.pop();
                stack_.push_literal(0);
                return true;
            }
            if (b_elem->literal()) {
                auto const &b = b_elem->literal()->value;
                stack_.pop();
                stack_.pop();
                if constexpr (is_sdiv) {
                    stack_.push_literal(b == 0 ? 0 : intx::sdivrem(a, b).quot);
                }
                else {
                    stack_.push_literal(b == 0 ? 0 : a / b);
                }
                return true;
            }
            return false;
        }
        else if (!b_elem->literal()) {
            return false;
        }

        auto b = std::move(b_elem)->literal()->value;
        if (b == 0) {
            stack_.pop();
            stack_.pop();
            stack_.push_literal(0);
            return true;
        }

        bool const needs_negation = [&] {
            if constexpr (is_sdiv) {
                if (b[3] & (static_cast<uint64_t>(1) << 63)) {
                    b = -b;
                    return true;
                }
            }
            return false;
        }();

        if (monad::utils::popcount(b) == 1) {
            stack_.pop();
            stack_.pop();
            auto shift = monad::utils::countr_zero(b);
            auto dst = [&] {
                if constexpr (is_sdiv) {
                    return sdiv_by_sar(a_elem, shift, {});
                }
                else {
                    return shift_by_literal<ShiftType::SHR>(shift, a_elem, {});
                }
            }();
            if (needs_negation) {
                stack_.push(negate(std::move(dst), {}));
            }
            else {
                stack_.push(std::move(dst));
            }
            return true;
        }

        return false;
    }

    template bool Emitter::div_optimized<true>();
    template bool Emitter::div_optimized<false>();

    // Discharge
    template <typename... LiveSet>
    StackElemRef Emitter::smod_by_mask(
        StackElemRef elem, uint256_t const &mask,
        std::tuple<LiveSet...> const &live)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!elem->literal().has_value());

        discharge_deferred_comparison();

        StackElemRef dst;
        if (elem->general_reg() && !is_live(elem, live)) {
            dst = stack_.release_general_reg(elem);
        }
        else {
            GeneralRegReserv const elem_reserv{elem};
            auto [r, _] = alloc_general_reg();
            dst = std::move(r);
            mov_stack_elem_to_gpq256(
                elem, general_reg_to_gpq256(*dst->general_reg()));
        }

        auto const &dst_gpq = general_reg_to_gpq256(*dst->general_reg());

        auto const &sign_lbl =
            append_literal(Literal{static_cast<uint64_t>(1) << 63});
        auto non_negative_lbl = as_.newLabel();
        auto after_lbl = as_.newLabel();

        auto emit_mask = [&] {
            if (is_literal_bounded({mask})) {
                for (size_t i = 0; i < 4; ++i) {
                    as_.and_(dst_gpq[i], mask[i]);
                }
            }
            else {
                auto const &lbl = append_literal({mask});
                auto m = x86::qword_ptr(lbl);
                for (size_t i = 0; i < 4; ++i) {
                    as_.and_(dst_gpq[i], m);
                    m.addOffset(8);
                }
            }
        };

        as_.test(x86::qword_ptr(sign_lbl), dst_gpq[3]);
        as_.jz(non_negative_lbl);
        negate_gpq256(dst_gpq);
        emit_mask();
        negate_gpq256(dst_gpq);
        as_.jmp(after_lbl);
        as_.bind(non_negative_lbl);
        emit_mask();
        as_.bind(after_lbl);

        return dst;
    }

    template <bool is_smod>
    bool Emitter::mod_optimized()
    {
        auto const a_elem = stack_.get(stack_.top_index());
        auto const b_elem = stack_.get(stack_.top_index() - 1);

        if (a_elem->literal()) {
            auto const &a = a_elem->literal()->value;
            if (a == 0) {
                stack_.pop();
                stack_.pop();
                stack_.push(std::move(a_elem));
                return true;
            }
            if (b_elem->literal()) {
                auto const &b = b_elem->literal()->value;
                stack_.pop();
                stack_.pop();
                if constexpr (is_smod) {
                    stack_.push_literal(b == 0 ? 0 : intx::sdivrem(a, b).rem);
                }
                else {
                    stack_.push_literal(b == 0 ? 0 : a % b);
                }
                return true;
            }
            return false;
        }
        else if (!b_elem->literal()) {
            return false;
        }

        auto b = std::move(b_elem)->literal()->value;
        if constexpr (is_smod) {
            if (b[3] & (static_cast<uint64_t>(1) << 63)) {
                b = -b;
            }
        }
        if (b <= 1) {
            stack_.pop();
            stack_.pop();
            stack_.push_literal(0);
            return true;
        }
        if (monad::utils::popcount(b) == 1) {
            stack_.pop();
            stack_.pop();
            if constexpr (is_smod) {
                stack_.push(smod_by_mask(std::move(a_elem), b - 1, {}));
            }
            else {
                stack_.push(
                    and_(std::move(a_elem), stack_.alloc_literal({b - 1}), {}));
                return true;
            }
            return true;
        }

        return false;
    }

    template bool Emitter::mod_optimized<true>();
    template bool Emitter::mod_optimized<false>();

    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::prepare_mod2_bin_dest_and_source(
        StackElemRef dst, StackElemRef src, size_t exp)
    {
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if (dst->literal() && !dst->stack_offset() && !dst->avx_reg() &&
            !dst->general_reg()) {
            if (src->general_reg()) {
                std::swap(dst, src);
            }
            else if (
                (src->stack_offset() || src->avx_reg()) &&
                is_literal_bounded(*dst->literal())) {
                std::swap(dst, src);
            }
        }

        if (dst.get() == src.get()) {
            if (!dst->general_reg()) {
                mov_stack_elem_to_general_reg_mod2(dst, exp);
            }
            return {
                std::move(dst),
                LocationType::GeneralReg,
                std::move(src),
                LocationType::GeneralReg};
        }

        if (dst->general_reg()) {
            if (src->general_reg()) {
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::GeneralReg};
            }
            if (src->stack_offset()) {
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::StackOffset};
            }
            if (src->literal()) {
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::Literal};
            }
            mov_avx_reg_to_stack_offset(src);
            return {
                std::move(dst),
                LocationType::GeneralReg,
                std::move(src),
                LocationType::StackOffset};
        }
        if (!dst->stack_offset()) {
            if (dst->literal()) {
                mov_literal_to_general_reg_mod2(dst, exp);
                if (src->general_reg()) {
                    return {
                        std::move(dst),
                        LocationType::GeneralReg,
                        std::move(src),
                        LocationType::GeneralReg};
                }
                if (src->stack_offset()) {
                    return {
                        std::move(dst),
                        LocationType::GeneralReg,
                        std::move(src),
                        LocationType::StackOffset};
                }
                if (src->literal()) {
                    return {
                        std::move(dst),
                        LocationType::GeneralReg,
                        std::move(src),
                        LocationType::Literal};
                }
                mov_avx_reg_to_stack_offset(src);
                return {
                    std::move(dst),
                    LocationType::GeneralReg,
                    std::move(src),
                    LocationType::StackOffset};
            }
            else {
                mov_avx_reg_to_stack_offset(dst);
                // Fall through
            }
        }
        if (src->general_reg()) {
            return {
                std::move(dst),
                LocationType::StackOffset,
                std::move(src),
                LocationType::GeneralReg};
        }
        if (src->literal() && is_literal_bounded(*src->literal())) {
            return {
                std::move(dst),
                LocationType::StackOffset,
                std::move(src),
                LocationType::Literal};
        }
        if (src->stack_offset()) {
            mov_stack_offset_to_general_reg_mod2(dst, exp);
            return {
                std::move(dst),
                LocationType::GeneralReg,
                std::move(src),
                LocationType::StackOffset};
        }
        if (src->literal()) {
            mov_stack_offset_to_general_reg_mod2(dst, exp);
            return {
                std::move(dst),
                LocationType::GeneralReg,
                std::move(src),
                LocationType::Literal};
        }
        mov_avx_reg_to_general_reg(src);
        return {
            std::move(dst),
            LocationType::StackOffset,
            std::move(src),
            LocationType::GeneralReg};
    }

    void
    Emitter::mov_stack_offset_to_general_reg_mod2(StackElemRef elem, size_t exp)
    {
        MONAD_COMPILER_DEBUG_ASSERT(exp > 0);
        MONAD_COMPILER_DEBUG_ASSERT(elem->stack_offset().has_value());

        x86::Mem mem{stack_offset_to_mem(*elem->stack_offset())};
        insert_general_reg(elem);
        MONAD_COMPILER_DEBUG_ASSERT(elem->general_reg());
        auto const &gpq = general_reg_to_gpq256(*elem->general_reg());

        size_t const numQwords = div64_ceil(exp);
        for (size_t i = 0; i < numQwords; i++) {
            size_t const occupied_bits =
                i + 1 == numQwords ? exp - (i * 64) : 64;
            if (occupied_bits <= 8) {
                as_.mov(gpq[i].r8Lo(), mem);
            }
            else if (occupied_bits <= 16) {
                as_.mov(gpq[i].r16(), mem);
            }
            else if (occupied_bits <= 32) {
                as_.mov(gpq[i].r32(), mem);
            }
            else {
                as_.mov(gpq[i].r64(), mem);
            }
            mem.addOffset(8);
        }
    }

    void Emitter::mov_literal_to_general_reg_mod2(StackElemRef elem, size_t exp)
    {
        MONAD_COMPILER_DEBUG_ASSERT(exp > 0);
        MONAD_COMPILER_DEBUG_ASSERT(elem->literal().has_value());

        insert_general_reg(elem);
        auto const &gpq = general_reg_to_gpq256(*elem->general_reg());
        auto const &lit =
            *elem->literal(); // literal_to_imm256(*elem->literal());
        size_t const numQwords = div64_ceil(exp);
        for (size_t i = 0; i < numQwords; i++) {
            size_t const occupied_bits =
                i + 1 == numQwords ? exp - (i * 64) : 64;
            if (occupied_bits <= 8) {
                as_.mov(gpq[i].r8Lo(), lit.value[i]);
            }
            else if (occupied_bits <= 16) {
                as_.mov(gpq[i].r16(), lit.value[i]);
            }
            else if (occupied_bits <= 32) {
                as_.mov(gpq[i].r32(), lit.value[i]);
            }
            else {
                as_.mov(gpq[i].r64(), lit.value[i]);
            }
        }
    }

    void
    Emitter::mov_stack_elem_to_general_reg_mod2(StackElemRef elem, size_t exp)
    {
        MONAD_COMPILER_DEBUG_ASSERT(exp > 0);
        if (elem->general_reg()) {
            return;
        }
        if (elem->literal()) {
            mov_literal_to_general_reg_mod2(std::move(elem), exp);
        }
        else if (elem->stack_offset()) {
            mov_stack_offset_to_general_reg_mod2(std::move(elem), exp);
        }
        else {
            MONAD_COMPILER_ASSERT(elem->avx_reg().has_value());
            mov_avx_reg_to_stack_offset(elem);
            mov_stack_offset_to_general_reg_mod2(std::move(elem), exp);
        }
    }

    template <typename... LiveSet>
    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::get_mod2_bin_dest_and_source(
        StackElemRef dst_in, StackElemRef src_in, size_t exp,
        std::tuple<LiveSet...> const &live)
    {
        auto [dst, dst_loc, src, src_loc] = prepare_mod2_bin_dest_and_source(
            std::move(dst_in), std::move(src_in), exp);
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if (dst_loc == LocationType::GeneralReg) {
            if (is_live(dst, live) && !dst->stack_offset() && !dst->literal() &&
                !dst->avx_reg()) {
                mov_general_reg_to_stack_offset(dst);
            }
            auto new_dst = stack_.release_general_reg(dst);
            if (dst.get() == src.get()) {
                return {new_dst, dst_loc, new_dst, src_loc};
            }
            else {
                return {std::move(new_dst), dst_loc, std::move(src), src_loc};
            }
        }
        MONAD_COMPILER_DEBUG_ASSERT(dst.get() != src.get());
        MONAD_COMPILER_DEBUG_ASSERT(dst_loc == LocationType::StackOffset);
        if (is_live(dst, live) && !dst->general_reg() && !dst->literal() &&
            !dst->avx_reg()) {
            mov_stack_offset_to_avx_reg(dst);
        }
        return {
            stack_.release_stack_offset(std::move(dst)),
            dst_loc,
            std::move(src),
            src_loc};
    }

    // Discharge
    template <
        Emitter::ModOpType ModOp, uint64_t Unit, uint64_t Absorb,
        Emitter::ModOpByMaskType ModOpByMask>
    bool Emitter::modop_optimized()
    {
        // required stack shape: [a b m]
        auto m_elem = stack_.get(stack_.top_index() - 2);
        if (!m_elem->literal()) {
            return false;
        }
        auto const &m = m_elem->literal()->value;

        // The trivial group
        if (m == 0 || m == 1) {
            stack_.pop();
            stack_.pop();
            stack_.pop();
            stack_.push_literal(0);
            return true;
        }

        auto a_elem = stack_.get(stack_.top_index());
        auto b_elem = stack_.get(stack_.top_index() - 1);

        static_assert(Absorb <= 1);
        static_assert(Unit <= 1);

        // Check whether we can constant fold the entire expression.
        if (a_elem->literal()) {
            if constexpr (Absorb != Unit) {
                if (a_elem->literal()->value == Absorb) {
                    stack_.pop();
                    stack_.pop();
                    stack_.pop();
                    push(Absorb);
                    return true;
                }
            }
            if (b_elem->literal()) {
                stack_.pop();
                stack_.pop();
                stack_.pop();
                auto const &a = a_elem->literal()->value;
                auto const &b = b_elem->literal()->value;
                push(ModOp(a, b, m));
                return true;
            }
        }
        else if (b_elem->literal()) {
            if constexpr (Absorb != Unit) {
                if (b_elem->literal()->value == Absorb) {
                    stack_.pop();
                    stack_.pop();
                    stack_.pop();
                    push(Absorb);
                    return true;
                }
            }
        }

        // Only proceed if we can rewrite
        // (a + b) % m, where m = 2^n
        // as
        // (a + b) & (n - 1)
        if (utils::popcount(m) != 1) {
            return false;
        }

        // Pop the operands
        stack_.pop();
        stack_.pop();
        stack_.pop();

        // Check whether we can elide the addition.
        if (b_elem->literal() && b_elem->literal()->value == Unit) {
            b_elem.reset(); // Clear to free registers and stack offset.
            auto mask = stack_.alloc_literal(Literal{m - 1});
            stack_.push(and_(std::move(a_elem), std::move(mask), {}));
        }
        else if (a_elem->literal() && a_elem->literal()->value == Unit) {
            a_elem.reset(); // Clear to free registers and stack offset.
            auto mask = stack_.alloc_literal(Literal{m - 1});
            stack_.push(and_(std::move(b_elem), std::move(mask), {}));
        }
        else {
            size_t const exp = utils::bit_width(m) - 1;
            // The heavy lifting is done by the following function.
            (this->*ModOpByMask)(std::move(a_elem), std::move(b_elem), exp);
        }

        return true;
    }

    // Discharge
    bool Emitter::addmod_opt()
    {
        return modop_optimized<intx::addmod, 0, 0, &Emitter::add_mod2>();
    }

    void Emitter::add_mod2(StackElemRef a_elem, StackElemRef b_elem, size_t exp)
    {
        discharge_deferred_comparison();

        auto [left, left_loc, right, right_loc] =
            get_mod2_bin_dest_and_source(a_elem, b_elem, exp, {});
        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(right, right_loc);
        MONAD_COMPILER_DEBUG_ASSERT(
            !std::holds_alternative<x86::Ymm>(right_op));

        size_t const numQwords = div64_ceil(exp);

        // Skip initial additions by zero.
        size_t start_offset = 0;
        if (right->literal()) {
            while (start_offset < numQwords) {
                uint64_t const mask =
                    start_offset + 1 == numQwords && (exp & 63) != 0
                        ? (uint64_t{1} << (exp & 63)) - 1
                        : std::numeric_limits<uint64_t>::max();
                if ((right->literal()->value[start_offset] & mask) != 0) {
                    break;
                }
                ++start_offset;
            }
        }

        // Common logic for emitting masks for a single destination
        // register or destination memory.
        auto emit_mask = [&](std::variant<x86::Gp, x86::Mem> const &dst) {
            std::visit(
                Cases{
                    [&](x86::Gp const &dst) {
                        if ((exp & 63) == 8) {
                            as_.movzx(dst.r64(), dst.r8Lo());
                            return;
                        }

                        if ((exp & 63) == 16) {
                            as_.movzx(dst.r64(), dst.r16());
                            return;
                        }

                        if (start_offset == numQwords) {
                            if ((exp & 63) == 0) {
                                return;
                            }
                        }
                        else {
                            if ((exp & 31) == 0) {
                                return;
                            }
                        }

                        uint64_t const mask =
                            (1ULL << uint64_t(exp % 64)) - 1ULL;
                        if (std::bit_width(mask) <= 32) {
                            as_.and_(dst.r32(), mask);
                        }
                        else {
                            as_.movabs(x86::rax, mask);
                            as_.and_(dst, x86::rax);
                        }
                    },
                    [&](x86::Mem const &dst) {
                        if ((exp & 63) == 0) {
                            return;
                        }
                        uint64_t const mask =
                            (1ULL << uint64_t(exp % 64)) - 1ULL;
                        if (std::bit_width(mask) < 32) {
                            as_.and_(dst, mask);
                        }
                        else {
                            as_.movabs(x86::rax, mask);
                            as_.and_(dst, x86::rax);
                        }
                    }},
                dst);
        };

        // Common logic for clearing the upper destination register(s)
        // or part(s) of the destination memory.
        auto clear_upper_dest = [&](std::variant<Gpq256, x86::Mem> const &dst) {
            std::visit(
                Cases{
                    [&](Gpq256 const &c) {
                        for (size_t i = numQwords; i < 4; i++) {
                            if (!stack_.has_deferred_comparison()) {
                                as_.xor_(c[i], c[i]);
                            }
                            else {
                                as_.mov(c[i], 0);
                            }
                        }
                    },
                    [&](x86::Mem const &c) {
                        x86::Mem temp{c};
                        for (size_t i = numQwords; i < 4; i++) {
                            temp.addOffset(8);
                            as_.mov(temp, 0);
                        }
                    }},
                dst);
        };

        // The general logic for computing (a + b) & (n - 1)
        if (std::holds_alternative<Gpq256>(left_op)) {
            Gpq256 const &a = std::get<Gpq256>(left_op);
            std::visit(
                Cases{
                    [&](Gpq256 const &b) {
                        for (size_t i = start_offset; i < numQwords; i++) {
                            size_t const bits_occupied =
                                i + 1 == numQwords ? exp - (i * 64) : 64;
                            if (i == start_offset) {
                                if (bits_occupied <= 32) {
                                    as_.add(a[i].r32(), b[i].r32());
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.add(a[i].r64(), b[i].r64());
                                }
                            }
                            else {
                                if (bits_occupied <= 32) {
                                    as_.adc(a[i].r32(), b[i].r32());
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.adc(a[i].r64(), b[i].r64());
                                }
                            }
                        }
                        emit_mask(a[numQwords - 1]);
                        clear_upper_dest(a);
                    },
                    [&](x86::Mem const &b) {
                        x86::Mem temp{b};
                        temp.addOffset(static_cast<int64_t>(start_offset) * 8);
                        for (size_t i = start_offset; i < numQwords; i++) {
                            size_t const bits_occupied =
                                i + 1 == numQwords ? exp - (i * 64) : 64;
                            if (i == start_offset) {
                                if (bits_occupied <= 8) {
                                    as_.add(a[i].r8Lo(), temp);
                                }
                                else if (bits_occupied <= 16) {
                                    as_.add(a[i].r16(), temp);
                                }
                                else if (bits_occupied <= 32) {
                                    as_.add(a[i].r32(), temp);
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.add(a[i].r64(), temp);
                                }
                            }
                            else {
                                if (bits_occupied <= 8) {
                                    as_.adc(a[i].r8Lo(), temp);
                                }
                                else if (bits_occupied <= 16) {
                                    as_.adc(a[i].r16(), temp);
                                }
                                else if (bits_occupied <= 32) {
                                    as_.adc(a[i].r32(), temp);
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.adc(a[i].r64(), temp);
                                }
                            }
                            temp.addOffset(8);
                        }
                        emit_mask(a[numQwords - 1]);
                        clear_upper_dest(a);
                    },
                    [&](Imm256 const &b) {
                        for (size_t i = start_offset; i < numQwords; i++) {
                            size_t const bits_occupied =
                                i + 1 == numQwords ? exp - (i * 64) : 64;
                            if (i == start_offset) {
                                if (bits_occupied <= 8) {
                                    as_.add(a[i].r8Lo(), b[i]);
                                }
                                else if (bits_occupied <= 16) {
                                    as_.add(a[i].r16(), b[i]);
                                }
                                else if (bits_occupied <= 32) {
                                    as_.add(a[i].r32(), b[i]);
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.add(a[i].r64(), b[i]);
                                }
                            }
                            else {
                                if (bits_occupied <= 8) {
                                    as_.adc(a[i].r8Lo(), b[i]);
                                }
                                else if (bits_occupied <= 16) {
                                    as_.adc(a[i].r16(), b[i]);
                                }
                                else if (bits_occupied <= 32) {
                                    as_.adc(a[i].r32(), b[i]);
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.adc(a[i].r64(), b[i]);
                                }
                            }
                        }
                        emit_mask(a[numQwords - 1]);
                        clear_upper_dest(a);
                    },
                    [](x86::Ymm const &) { std::unreachable(); },
                },
                right_op);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(
                std::holds_alternative<x86::Mem>(left_op));
            MONAD_COMPILER_DEBUG_ASSERT(
                !std::holds_alternative<x86::Mem>(right_op));

            x86::Mem const &a = std::get<x86::Mem>(left_op);
            std::visit(
                Cases{
                    [&](Gpq256 const &b) {
                        x86::Mem temp{a};
                        temp.addOffset(
                            static_cast<int64_t>(start_offset) * 8 - 8);
                        for (size_t i = start_offset; i < numQwords; i++) {
                            temp.addOffset(8);
                            size_t const bits_occupied =
                                i + 1 == numQwords ? exp - (i * 64) : 64;
                            if (i == start_offset) {
                                if (bits_occupied <= 8) {
                                    as_.add(temp, b[i].r8Lo());
                                }
                                else if (bits_occupied <= 16) {
                                    as_.add(temp, b[i].r16());
                                }
                                else if (bits_occupied <= 32) {
                                    as_.add(temp, b[i].r32());
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.add(temp, b[i].r64());
                                }
                            }
                            else {
                                if (bits_occupied <= 8) {
                                    as_.adc(temp, b[i].r8Lo());
                                }
                                else if (bits_occupied <= 16) {
                                    as_.adc(temp, b[i].r16());
                                }
                                else if (bits_occupied <= 32) {
                                    as_.adc(temp, b[i].r32());
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.adc(temp, b[i].r64());
                                }
                            }
                        };
                        emit_mask(temp);
                        clear_upper_dest(temp);
                    },
                    [&](Imm256 const &b) {
                        x86::Mem temp{a};
                        temp.addOffset(
                            static_cast<int64_t>(start_offset) * 8 - 8);
                        for (size_t i = start_offset; i < numQwords; i++) {
                            temp.addOffset(8);
                            size_t const bits_occupied =
                                i + 1 == numQwords ? exp - (i * 64) : 64;
                            if (i == start_offset) {
                                if (bits_occupied <= 8) {
                                    temp.setSize(1);
                                    as_.add(temp, b[i]);
                                }
                                else if (bits_occupied <= 16) {
                                    temp.setSize(2);
                                    as_.add(temp, b[i]);
                                }
                                else if (bits_occupied <= 32) {
                                    temp.setSize(4);
                                    as_.add(temp, b[i]);
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.add(temp, b[i]);
                                }
                            }
                            else {
                                if (bits_occupied <= 8) {
                                    temp.setSize(1);
                                    as_.adc(temp, b[i]);
                                }
                                else if (bits_occupied <= 16) {
                                    temp.setSize(2);
                                    as_.adc(temp, b[i]);
                                }
                                else if (bits_occupied <= 32) {
                                    temp.setSize(4);
                                    as_.adc(temp, b[i]);
                                }
                                else {
                                    MONAD_COMPILER_DEBUG_ASSERT(
                                        bits_occupied <= 64);
                                    as_.adc(temp, b[i]);
                                }
                            }
                        };
                        temp.setSize(8);
                        emit_mask(temp);
                        clear_upper_dest(temp);
                    },
                    [](auto const &) { std::unreachable(); },
                },
                right_op);
        }
        stack_.push(std::move(left));
    }

    // Discharge
    bool Emitter::mulmod_opt()
    {
        return modop_optimized<intx::mulmod, 1, 0, &Emitter::mul_mod2>();
    }

    void Emitter::mul_mod2(StackElemRef a_elem, StackElemRef b_elem, size_t exp)
    {
        discharge_deferred_comparison();

        MONAD_COMPILER_DEBUG_ASSERT(exp >= 1 && exp < 256);
        if (a_elem->literal()) {
            std::swap(a_elem, b_elem);
        }
        MONAD_COMPILER_DEBUG_ASSERT(!a_elem->literal().has_value());

        auto mask = (1 << uint256_t{exp}) - 1;
        auto const last_ix = (exp - 1) >> 6;
        static constexpr size_t inline_threshold = 1;

        // We will inline the multiplication in two cases.
        // 1. If the number of qwords is at most `inline_threshold + 1`,
        //    then inline the multiplication to avoid overhead of a
        //    runtime call.
        // 2. If multiplying by a known literal and one qword of the
        //    literal is zero, then inline to save at least one x86
        //    multiplication instruction.
        if (b_elem->literal()) {
            auto b = b_elem->literal()->value & mask;
            bool has_zero = false;
            for (size_t i = 0; i <= last_ix; ++i) {
                has_zero |= b[i] == 0;
            }
            if (last_ix <= inline_threshold || has_zero) {
                b_elem.reset(); // Clear registers.
                stack_.push(mul_with_bit_size(exp, std::move(a_elem), b, {}));
                return;
            }
        }
        else if (last_ix <= inline_threshold) {
            if (b_elem->general_reg()) {
                auto const &b = general_reg_to_gpq256(*b_elem->general_reg());
                GeneralRegReserv const b_reserv{b_elem};
                stack_.push(mul_with_bit_size(
                    exp, std::move(a_elem), b, std::make_tuple(b_elem)));
            }
            else {
                if (!b_elem->stack_offset()) {
                    mov_avx_reg_to_stack_offset(b_elem);
                }
                auto const &b = stack_offset_to_mem(*b_elem->stack_offset());
                stack_.push(mul_with_bit_size(
                    exp, std::move(a_elem), b, std::make_tuple(b_elem)));
            }
            return;
        }

        MONAD_COMPILER_DEBUG_ASSERT(exp > 128);
        spill_caller_save_regs(false);

        auto call_runtime_mul = [&](RuntimeImpl &&rt) {
            rt.pass(std::move(a_elem));
            rt.pass(std::move(b_elem));
            rt.call_impl();
        };
        if (exp <= 192) {
            call_runtime_mul(
                Runtime<uint256_t *, uint256_t const *, uint256_t const *>(
                    this, false, monad_runtime_mul_192));
        }
        else {
            call_runtime_mul(
                Runtime<uint256_t *, uint256_t const *, uint256_t const *>(
                    this, false, monad::runtime::mul));
        }

        MONAD_COMPILER_DEBUG_ASSERT(stack_.top()->stack_offset().has_value());
        auto res_mem = stack_offset_to_mem(*stack_.top()->stack_offset());
        res_mem.addOffset(static_cast<int64_t>(last_ix * 8));
        if (exp & 63) {
            auto last_mask = mask[last_ix];
            if (std::bit_width(last_mask) < 32) {
                as_.and_(res_mem, last_mask);
            }
            else {
                as_.mov(x86::rax, last_mask);
                as_.and_(res_mem, x86::rax);
            }
        }
        if (last_ix < 3) {
            res_mem.addOffset(8);
            MONAD_COMPILER_DEBUG_ASSERT(last_ix == 2);
            as_.mov(res_mem, 0);
        }
    }
}
