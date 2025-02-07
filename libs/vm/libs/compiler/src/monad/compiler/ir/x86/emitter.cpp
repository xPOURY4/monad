#include <monad/compiler/ir/basic_blocks.hpp>
#include <monad/compiler/ir/x86.hpp>
#include <monad/compiler/ir/x86/emitter.hpp>
#include <monad/compiler/ir/x86/virtual_stack.hpp>
#include <monad/compiler/types.hpp>
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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
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

    bool is_literal_bounded(Literal lit)
    {
        constexpr int64_t upper =
            static_cast<int64_t>(std::numeric_limits<int32_t>::max());
        constexpr int64_t lower =
            static_cast<int64_t>(std::numeric_limits<int32_t>::min());
        for (size_t i = 0; i < 4; ++i) {
            auto v = static_cast<int64_t>(lit.value[i]);
            if (v > upper || v < lower) {
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

        em_->as_.vzeroupper();
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

    ////////// Initialization and de-initialization //////////

    Emitter::Emitter(
        asmjit::JitRuntime const &rt, uint64_t codesize, char const *log_path)
        : as_{init_code_holder(rt, log_path)}
        , epilogue_label_{as_.newNamedLabel("ContractEpilogue")}
        , error_label_{as_.newNamedLabel("Error")}
        , jump_table_label_{as_.newNamedLabel("JumpTable")}
        , keep_stack_in_next_block_{}
        , gpq256_regs_{Gpq256{x86::r12, x86::r13, x86::r14, x86::r15}, Gpq256{x86::r8, x86::r9, x86::r10, x86::r11}, Gpq256{x86::rcx, x86::rdx, x86::rsi, x86::rdi}}
        , rcx_general_reg{2}
        , rcx_general_reg_index{}
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
            as_.mov(rpq[1], rpq[0]);
            as_.mov(rpq[2], rpq[0]);
            as_.mov(rpq[3], rpq[0]);
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

    ////////// Private initialization and de-initialization //////////

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

    ////////// Debug functionality //////////

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
        spill_all_caller_save_regs();
        as_.lea(x86::rdi, x86::qword_ptr(msg_lbl));
        as_.mov(x86::rsi, reg_context);
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
        spill_all_caller_save_regs();
        as_.lea(x86::rdi, x86::qword_ptr(msg_lbl));
        as_.mov(x86::rsi, reg_stack);
        as_.mov(x86::rdx, x86::qword_ptr(x86::rsp, sp_offset_stack_size));
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
        spill_all_caller_save_regs();

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
        as_.call(x86::qword_ptr(fn_lbl));
    }

    void Emitter::breakpoint()
    {
        as_.int3();
    }

    ////////// Core emit functionality //////////

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
        as_.jb(error_label_);
    }

    void Emitter::spill_all_caller_save_regs()
    {
        // Spill general regs first, because if stack element is in both
        // general register and avx register then stack element will be
        // moved to stack using avx register.
        spill_all_caller_save_general_regs();
        spill_all_avx_regs();
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

    void Emitter::adjust_by_stack_delta()
    {
        auto const delta = stack_.delta();
        if (delta != 0) {
            as_.add(x86::qword_ptr(x86::rsp, sp_offset_stack_size), delta);
            as_.add(x86::rbp, delta * 32);
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

    ////////// Private debug functionality //////////

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

    ////////// Private core emit functionality //////////

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

    ////////// Move functionality //////////

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

    ////////// Private move functionality //////////

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
        for (size_t i = 0; i < 4; ++i) {
            as_.mov(gpq[i], lit.value[i]);
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
        auto const &rs = general_reg_to_gpq256(*elem->general_reg());
        auto lit = *elem->literal();
        x86::Gpq const *zero_reg = nullptr;
        for (size_t i = 0; i < 4; ++i) {
            auto const &r = rs[i];
            if (lit.value[i] == 0) {
                if (zero_reg) {
                    as_.mov(r, *zero_reg);
                }
                else {
                    if (!stack_.has_deferred_comparison()) {
                        as_.xor_(r, r);
                    }
                    else {
                        as_.mov(r, 0);
                    }
                    zero_reg = &r;
                }
            }
            else {
                as_.mov(r, lit.value[i]);
            }
        }
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
        stack_.unsafe_move_general_reg(*reg_elem(), *elem);

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

    ////////// EVM instructions //////////

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

    // Discharge
    void Emitter::sub()
    {
        auto pre_dst = stack_.pop();
        auto pre_src = stack_.pop();

        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                push(x - y);
                return;
            }
        }
        else if (pre_src->literal() && pre_src->literal()->value == 0) {
            stack_.push(std::move(pre_dst));
            return;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<false>(
            std::move(pre_dst), stack_.top_index() + 1, std::move(pre_src), {});

        auto dst_op = get_operand(dst, dst_loc);
        auto src_op = get_operand(src, src_loc);
        GENERAL_BIN_INSTR(sub, sbb)(dst_op, src_op);

        stack_.push(std::move(dst));
    }

    // Discharge
    void Emitter::add()
    {
        auto pre_dst = stack_.pop();
        auto pre_src = stack_.pop();

        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                push(x + y);
                return;
            }
            else if (pre_dst->literal()->value == 0) {
                stack_.push(std::move(pre_src));
                return;
            }
        }
        else if (pre_src->literal() && pre_src->literal()->value == 0) {
            stack_.push(std::move(pre_dst));
            return;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<true>(
            std::move(pre_dst), stack_.top_index() + 1, std::move(pre_src), {});

        auto dst_op = get_operand(dst, dst_loc);
        auto src_op = get_operand(src, src_loc);
        GENERAL_BIN_INSTR(add, adc)(dst_op, src_op);

        stack_.push(std::move(dst));
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

    // Discharge through `shift_by_stack_elem`
    void Emitter::shl()
    {
        auto shift = stack_.pop();
        auto value = stack_.pop();

        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal()->value;
            auto const &x = value->literal()->value;
            push(x << i);
            return;
        }

        shift_by_stack_elem<ShiftType::SHL>(
            std::move(shift), std::move(value), {});
    }

    // Discharge through `shift_by_stack_elem`
    void Emitter::shr()
    {
        auto shift = stack_.pop();
        auto value = stack_.pop();

        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal()->value;
            auto const &x = value->literal()->value;
            push(x >> i);
            return;
        }

        shift_by_stack_elem<ShiftType::SHR>(
            std::move(shift), std::move(value), {});
    }

    // Discharge through `shift_by_stack_elem`
    void Emitter::sar()
    {
        auto shift = stack_.pop();
        auto value = stack_.pop();

        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal()->value;
            auto const &x = value->literal()->value;
            push(monad::utils::sar(i, x));
            return;
        }

        shift_by_stack_elem<ShiftType::SAR>(
            std::move(shift), std::move(value), {});
    }

    // Discharge
    void Emitter::and_()
    {
        auto pre_dst = stack_.pop();
        auto pre_src = stack_.pop();

        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                push(x & y);
                return;
            }
            if (pre_dst->literal()->value ==
                std::numeric_limits<uint256_t>::max()) {
                stack_.push(std::move(pre_src));
                return;
            }
        }
        else if (
            pre_src->literal() && pre_src->literal()->value ==
                                      std::numeric_limits<uint256_t>::max()) {
            stack_.push(std::move(pre_dst));
            return;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src), {});

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(and_, vpand)(dst, left_op, right_op);

        stack_.push(std::move(dst));
    }

    // Discharge
    void Emitter::or_()
    {
        auto pre_dst = stack_.pop();
        auto pre_src = stack_.pop();

        if (pre_dst->literal()) {
            if (pre_src->literal()) {
                auto const &x = pre_dst->literal()->value;
                auto const &y = pre_src->literal()->value;
                push(x | y);
                return;
            }
            if (pre_dst->literal()->value == 0) {
                stack_.push(std::move(pre_src));
                return;
            }
        }
        else if (pre_src->literal() && pre_src->literal()->value == 0) {
            stack_.push(std::move(pre_dst));
            return;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src), {});

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(or_, vpor)(dst, left_op, right_op);

        stack_.push(std::move(dst));
    }

    // Discharge
    void Emitter::xor_()
    {
        auto pre_dst = stack_.pop();
        auto pre_src = stack_.pop();

        if (pre_dst == pre_src) {
            push(0);
            return;
        }
        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal()->value;
            auto const &y = pre_src->literal()->value;
            push(x ^ y);
            return;
        }

        discharge_deferred_comparison();

        // Empty live set, because only `pre_dst` and `pre_src` are live:
        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src), {});

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(xor_, vpxor)(dst, left_op, right_op);

        stack_.push(std::move(dst));
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

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(xor_, vpxor)(dst, left_op, right_op);

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
        as_.mov(gpq[2], gpq[1]);
        as_.mov(gpq[3], gpq[1]);
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
        spill_all_caller_save_regs();
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
        adjust_by_stack_delta();
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

    ////////// Private EVM instruction utilities //////////

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
        StackElemRef &&dest, std::tuple<LiveSet...> const &live)
    {
        if (dest->literal()) {
            auto lit = literal_jump_dest_operand(std::move(dest));
            write_to_final_stack_offsets();
            adjust_by_stack_delta();
            jump_literal_dest(lit);
        }
        else {
            auto [op, spill_elem] = non_literal_jump_dest_operand(dest, live);
            write_to_final_stack_offsets();
            adjust_by_stack_delta();
            jump_non_literal_dest(dest, op, spill_elem);
        }
    }

    uint256_t Emitter::literal_jump_dest_operand(StackElemRef &&dest)
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
            // Restore `stack_` back to the state before calling
            // `non_literal_jump_dest_operand`.
            auto *e = *spill_elem;
            if (e != nullptr) {
                stack_.unsafe_move_general_reg(*dest, *e);
            }
            else {
                stack_.unsafe_remove_general_reg(*dest);
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

    Comparison Emitter::jumpi_comparison(StackElemRef &&cond, StackElemRef dest)
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
                adjust_by_stack_delta();
            }
            else {
                // Clear to remove locations, if not on stack:
                cond = nullptr;
                jump_stack_elem_dest(std::move(dest), {});
            }
            return;
        }

        Comparison const comp = jumpi_comparison(std::move(cond), dest);

        auto fallthrough_lbl = as_.newLabel();
        if (dest->literal()) {
            auto lit = literal_jump_dest_operand(std::move(dest));
            write_to_final_stack_offsets();
            conditional_jmp(fallthrough_lbl, negate_comparison(comp));
            adjust_by_stack_delta();
            as_.jmp(jump_dest_label(lit));
        }
        else {
            // Note that `cond` is not live here.
            auto [op, spill_elem] = non_literal_jump_dest_operand(dest, {});
            write_to_final_stack_offsets();
            conditional_jmp(fallthrough_lbl, negate_comparison(comp));
            adjust_by_stack_delta();
            jump_non_literal_dest(dest, op, spill_elem);
        }

        as_.bind(fallthrough_lbl);
        adjust_by_stack_delta();
    }

    void Emitter::jumpi_keep_fallthrough_stack()
    {
        keep_stack_in_next_block_ = true;

        auto dest = stack_.pop();
        auto cond = stack_.pop();

        if (cond->literal()) {
            if (cond->literal()->value != 0) {
                discharge_deferred_comparison();
                // Clear to remove locations, if not on stack:
                cond = nullptr;
                jump_stack_elem_dest(std::move(dest), {});
            }
            return;
        }

        asmjit::Label const fallthrough_lbl = as_.newLabel();
        Comparison const comp = jumpi_comparison(std::move(cond), dest);
        conditional_jmp(fallthrough_lbl, negate_comparison(comp));
        jump_stack_elem_dest(std::move(dest), {});
        as_.bind(fallthrough_lbl);
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
        }
        else {
            as_.xor_(gpq[1], gpq[1]);
        }
        as_.mov(gpq[2], gpq[1]);
        as_.mov(gpq[3], gpq[1]);
        stack_.push(std::move(dst));
    }

    void Emitter::read_context_uint64_to_word(int32_t offset)
    {
        auto [dst, _] = alloc_general_reg();
        Gpq256 const &gpq = general_reg_to_gpq256(*dst->general_reg());
        as_.mov(gpq[0], x86::qword_ptr(reg_context, offset));
        if (stack_.has_deferred_comparison()) {
            as_.mov(gpq[1], 0);
        }
        else {
            as_.xor_(gpq[1], gpq[1]);
        }
        as_.mov(gpq[2], gpq[1]);
        as_.mov(gpq[3], gpq[1]);
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
        auto dst_op = get_operand(dst, dst_loc);
        auto src_op = get_operand(src, src_loc);
        GENERAL_BIN_INSTR(cmp, sbb)(dst_op, src_op);
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
        as_.mov(gpq[1], gpq[0]);
        as_.mov(gpq[2], gpq[0]);
        as_.mov(gpq[3], gpq[0]);
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
        as_.mov(dst_gpq[2], dst_gpq[1]);
        as_.mov(dst_gpq[3], dst_gpq[1]);
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

    template <Emitter::ShiftType shift_type, typename... LiveSet>
    void Emitter::shift_by_stack_elem(
        StackElemRef shift, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        RegReserv const ix_reserv{shift};
        RegReserv const src_reserv{value};

        discharge_deferred_comparison();

        if (shift->literal()) {
            shift_by_literal<shift_type>(
                shift->literal()->value, std::move(value), live);
            return;
        }
        if (shift->general_reg()) {
            shift_by_general_reg_or_stack_offset<shift_type>(
                std::move(shift), std::move(value), live);
            return;
        }
        if (!shift->stack_offset()) {
            mov_avx_reg_to_stack_offset(shift);
        }
        shift_by_general_reg_or_stack_offset<shift_type>(
            std::move(shift), std::move(value), live);
    }

    template <Emitter::ShiftType shift_type, typename... LiveSet>
    void Emitter::setup_shift_stack(
        StackElemRef value, std::tuple<LiveSet...> const &live)
    {
        if constexpr (shift_type == ShiftType::SHL) {
            mov_literal_to_unaligned_mem(Literal{0}, qword_ptr(x86::rsp, -64));
            mov_stack_elem_to_unaligned_mem(value, qword_ptr(x86::rsp, -32));
        }
        else if constexpr (shift_type == ShiftType::SHR) {
            mov_stack_elem_to_unaligned_mem(value, qword_ptr(x86::rsp, -64));
            mov_literal_to_unaligned_mem(Literal{0}, qword_ptr(x86::rsp, -32));
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
            as_.mov(qword_ptr(x86::rsp, -32), reg);
            as_.mov(qword_ptr(x86::rsp, -24), reg);
            as_.mov(qword_ptr(x86::rsp, -16), reg);
            as_.mov(qword_ptr(x86::rsp, -8), reg);
        }
    }

    template <Emitter::ShiftType shift_type, typename... LiveSet>
    void Emitter::shift_by_literal(
        uint256_t shift, StackElemRef value, std::tuple<LiveSet...> const &live)
    {
        if (shift >= 256) {
            if constexpr (
                shift_type == ShiftType::SHL || shift_type == ShiftType::SHR) {
                push(0);
                return;
            }
            else {
                shift = 256;
            }
        }
        else if (shift == 0) {
            stack_.push(std::move(value));
            return;
        }

        int32_t const s = static_cast<int32_t>(shift[0]);
        int8_t const c = static_cast<int8_t>(s & 7);
        int32_t const d = s >> 3;

        if (d > 0) {
            setup_shift_stack<shift_type>(std::move(value), live);
        }

        auto [dst, dst_reserv] =
            [&] -> std::pair<StackElemRef, GeneralRegReserv> {
            if (d > 0) {
                return alloc_general_reg();
            }
            else {
                if (!is_live(value, live) && value->general_reg()) {
                    auto r = stack_.release_general_reg(std::move(value));
                    return {r, GeneralRegReserv{r}};
                }
                else {
                    auto [r, reserv] = alloc_general_reg();
                    mov_stack_elem_to_gpq256(
                        std::move(value),
                        general_reg_to_gpq256(*r->general_reg()));
                    return {r, reserv};
                }
            }
        }();

        Gpq256 const &dst_gpq = general_reg_to_gpq256(*dst->general_reg());

        if constexpr (shift_type == ShiftType::SHL) {
            if (d > 0) {
                as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, -8 - d));
                as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, -16 - d));
                as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, -24 - d));
                as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, -32 - d));
            }
            if (c > 0) {
                as_.shld(dst_gpq[3], dst_gpq[2], c);
                as_.shld(dst_gpq[2], dst_gpq[1], c);
                as_.shld(dst_gpq[1], dst_gpq[0], c);
                as_.shl(dst_gpq[0], c);
            }
        }
        else {
            if (d > 0) {
                as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, d - 40));
                as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, d - 48));
                as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, d - 56));
                as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, d - 64));
            }
            if (c > 0) {
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
        }

        stack_.push(std::move(dst));
    }

    template <Emitter::ShiftType shift_type, typename... LiveSet>
    void Emitter::shift_by_general_reg_or_stack_offset(
        StackElemRef shift, StackElemRef value,
        std::tuple<LiveSet...> const &live)
    {
        if (value->literal()) {
            if (value->literal()->value == 0) {
                stack_.push(std::move(value));
                return;
            }
            if constexpr (shift_type == ShiftType::SAR) {
                if (value->literal()->value ==
                    std::numeric_limits<uint256_t>::max()) {
                    stack_.push(std::move(value));
                    return;
                }
            }
        }

        setup_shift_stack<shift_type>(
            std::move(value), std::tuple_cat(std::make_tuple(shift), live));

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

        stack_.push(std::move(dst));
    }

    template <bool commutative>
    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::prepare_general_dest_and_source(
        StackElemRef dst, std::optional<int32_t> dst_ix, StackElemRef src)
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

        if (dst_ix && dst->stack_offset()) {
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
            mov_avx_reg_to_stack_offset(src);
            return {
                std::move(dst),
                LocationType::GeneralReg,
                std::move(src),
                LocationType::StackOffset};
        }
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
            mov_stack_offset_to_general_reg(src);
            return {
                std::move(dst),
                LocationType::StackOffset,
                std::move(src),
                LocationType::GeneralReg};
        }
        if (src->literal()) {
            mov_literal_to_general_reg(src);
            return {
                std::move(dst),
                LocationType::StackOffset,
                std::move(src),
                LocationType::GeneralReg};
        }
        mov_avx_reg_to_general_reg(src);
        return {
            std::move(dst),
            LocationType::StackOffset,
            std::move(src),
            LocationType::GeneralReg};
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
                std::move(dst_in), dst_ix, std::move(src_in));
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
        default:
            std::unreachable();
        }
    }

    template <
        Emitter::GeneralBinInstr<x86::Gp, x86::Gp> GG,
        Emitter::GeneralBinInstr<x86::Gp, x86::Mem> GM,
        Emitter::GeneralBinInstr<x86::Gp, asmjit::Imm> GI,
        Emitter::GeneralBinInstr<x86::Mem, x86::Gp> MG,
        Emitter::GeneralBinInstr<x86::Mem, asmjit::Imm> MI>
    void
    Emitter::general_bin_instr(Operand const &dst_op, Operand const &src_op)
    {
        if (std::holds_alternative<Gpq256>(dst_op)) {
            Gpq256 const &dst_gpq = std::get<Gpq256>(dst_op);
            std::visit(
                Cases{
                    [&](Gpq256 const &src_gpq) {
                        for (size_t i = 0; i < 4; ++i) {
                            (as_.*GG[i])(dst_gpq[i], src_gpq[i]);
                        }
                    },
                    [&](x86::Mem const &src_mem) {
                        x86::Mem temp{src_mem};
                        for (size_t i = 0; i < 4; ++i) {
                            (as_.*GM[i])(dst_gpq[i], temp);
                            temp.addOffset(8);
                        }
                    },
                    [&](Imm256 const &src_imm) {
                        for (size_t i = 0; i < 4; ++i) {
                            (as_.*GI[i])(dst_gpq[i], src_imm[i]);
                        }
                    },
                    [](x86::Ymm const &) { std::unreachable(); },
                },
                src_op);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(
                std::holds_alternative<x86::Mem>(dst_op));
            x86::Mem const &dst_mem = std::get<x86::Mem>(dst_op);
            std::visit(
                Cases{
                    [&](Gpq256 const &src_gpq) {
                        x86::Mem temp{dst_mem};
                        for (size_t i = 0; i < 4; ++i) {
                            (as_.*MG[i])(temp, src_gpq[i]);
                            temp.addOffset(8);
                        }
                    },
                    [&](Imm256 const &src_imm) {
                        x86::Mem temp{dst_mem};
                        for (size_t i = 0; i < 4; ++i) {
                            (as_.*MI[i])(temp, src_imm[i]);
                            temp.addOffset(8);
                        }
                    },
                    [](auto const &) { MONAD_COMPILER_ASSERT(false); },
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
                    if (is_live(d, live)) {
                        mov_stack_offset_to_avx_reg(s);
                        return priority_1(s, d);
                    }
                    mov_stack_offset_to_avx_reg(d);
                    return priority_1(d, s);
                }
                if (s->literal()) {
                    if (is_live(d, live)) {
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
        StackElemRef dst, Operand const &left, Operand const &right)
    {
        if (std::holds_alternative<Gpq256>(left)) {
            general_bin_instr<GG, GM, GI, MG, MI>(
                std::move(left), std::move(right));
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(dst->avx_reg().has_value());
            MONAD_COMPILER_DEBUG_ASSERT(std::holds_alternative<x86::Ymm>(left));
            if (std::holds_alternative<x86::Ymm>(right)) {
                (as_.*VV)(
                    avx_reg_to_ymm(*dst->avx_reg()),
                    std::get<x86::Ymm>(left),
                    std::get<x86::Ymm>(right));
            }
            else {
                MONAD_COMPILER_DEBUG_ASSERT(
                    std::holds_alternative<x86::Mem>(right));
                (as_.*VM)(
                    avx_reg_to_ymm(*dst->avx_reg()),
                    std::get<x86::Ymm>(left),
                    std::get<x86::Mem>(right));
            }
        }
    }
}
