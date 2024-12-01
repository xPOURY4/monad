#include "compiler/ir/x86/emitter.h"
#include "asmjit/core/api-config.h"
#include "asmjit/core/codeholder.h"
#include "asmjit/core/emitter.h"
#include "asmjit/core/globals.h"
#include "asmjit/core/jitruntime.h"
#include "asmjit/core/operand.h"
#include "asmjit/x86/x86operand.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/x86.h"
#include "compiler/ir/x86/virtual_stack.h"
#include "compiler/types.h"
#include "intx/intx.hpp"
#include "runtime/types.h"
#include "utils/assert.h"
#include "utils/uint256.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace runtime = monad::runtime;
namespace x86 = asmjit::x86;

static_assert(ASMJIT_ARCH_X86 == 64);

constexpr auto reg_scratch = x86::rax;
constexpr auto reg_context = x86::rbx;
constexpr auto reg_stack = x86::rbp;

constexpr auto context_offset_gas_remaining =
    offsetof(runtime::Context, gas_remaining);
constexpr auto context_offset_recipient =
    offsetof(runtime::Context, env) + offsetof(runtime::Environment, recipient);
constexpr auto context_offset_sender =
    offsetof(runtime::Context, env) + offsetof(runtime::Environment, sender);
constexpr auto context_offset_value =
    offsetof(runtime::Context, env) + offsetof(runtime::Environment, value);

constexpr auto result_offset_offset = offsetof(runtime::Result, offset);
constexpr auto result_offset_size = offsetof(runtime::Result, size);
constexpr auto result_offset_status = offsetof(runtime::Result, status);

constexpr auto sp_offset_arg1 = 0;
constexpr auto sp_offset_arg2 = sp_offset_arg1 + 8;
constexpr auto sp_offset_arg3 = sp_offset_arg2 + 8;
constexpr auto sp_offset_arg4 = sp_offset_arg3 + 8;
constexpr auto sp_offset_arg5 = sp_offset_arg4 + 8;
constexpr auto sp_offset_stack_size = sp_offset_arg5 + 8;
constexpr auto sp_offset_result_ptr = sp_offset_stack_size + 8;

constexpr auto stack_frame_size = sp_offset_result_ptr + 8;

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
        MONAD_COMPILER_DEBUG_ASSERT(is_literal_bounded(lit));
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

    ////////// Initialization and de-initialization //////////

    Emitter::Emitter(asmjit::JitRuntime const &rt, char const *log_path)
        : as_{init_code_holder(rt, log_path)}
        , epilogue_label_{as_.newNamedLabel("ContractEpilogue")}
        , out_of_gas_label_{as_.newNamedLabel("OutOfGas")}
        , overflow_label_{as_.newNamedLabel("Overflow")}
        , underflow_label_{as_.newNamedLabel("Underflow")}
        , gpq256_regs_{Gpq256{x86::r12, x86::r13, x86::r14, x86::r15}, Gpq256{x86::r8, x86::r9, x86::r10, x86::r11}, Gpq256{x86::rcx, x86::rdx, x86::rsi, x86::rdi}}
        , rcx_general_reg{2}
        , rcx_general_reg_index{}
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

        for (auto const &[lbl, op, back] : shift_out_of_bounds_handlers_) {
            as_.align(asmjit::AlignMode::kCode, 16);
            as_.bind(lbl);
            if (std::holds_alternative<Gpq256>(op)) {
                as_.mov(std::get<Gpq256>(op)[0], 256);
            }
            else {
                as_.mov(std::get<x86::Mem>(op), 256);
            }
            as_.jmp(back);
        }

        for (auto const &[lbl, rpq, back] : byte_out_of_bounds_handlers_) {
            as_.align(asmjit::AlignMode::kCode, 16);
            as_.bind(lbl);
            as_.xor_(rpq[0], rpq[0]);
            as_.mov(rpq[1], rpq[0]);
            as_.mov(rpq[2], rpq[0]);
            as_.mov(rpq[3], rpq[0]);
            as_.jmp(back);
        }

        error_block(out_of_gas_label_, runtime::StatusCode::OutOfGas);
        error_block(overflow_label_, runtime::StatusCode::Overflow);
        error_block(underflow_label_, runtime::StatusCode::Underflow);

        // TODO jumptable

        static char const *const ro_section_name = "ro";
        static auto const ro_section_name_len = 2;
        static auto const ro_section_index = 1;

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
        // rdi: result pointer
        // rsi: context pointer
        // rdx: stack pointer

        as_.push(x86::rbp); // 16 byte aligned
        as_.push(x86::rbx); // unaligned
        as_.push(x86::r12); // 16 byte aligned
        as_.push(x86::r13); // unaligned
        as_.push(x86::r14); // 16 byte aligned
        as_.push(x86::r15); // unaligned

        static_assert(stack_frame_size % 16 == 8);
        as_.sub(x86::rsp, stack_frame_size); // 16 byte aligned

        as_.mov(x86::ptr(x86::rsp, sp_offset_result_ptr), x86::rdi);
        as_.mov(reg_context, x86::rsi);
        as_.mov(reg_stack, x86::rdx);
        as_.mov(x86::qword_ptr(x86::rsp, sp_offset_stack_size), 0);
    }

    void Emitter::contract_epilogue()
    {
        as_.align(asmjit::AlignMode::kCode, 16);
        as_.bind(epilogue_label_);
        as_.add(x86::rsp, stack_frame_size);
        as_.pop(x86::r15);
        as_.pop(x86::r14);
        as_.pop(x86::r13);
        as_.pop(x86::r12);
        as_.pop(x86::rbx);
        as_.pop(x86::rbp);
        as_.ret();
    }

    ////////// Core emit functionality //////////

    Stack &Emitter::get_stack()
    {
        return *stack_;
    }

    void Emitter::begin_stack(Block const &b)
    {
        stack_ = std::make_unique<Stack>(b);
    }

    bool Emitter::block_prologue(Block const &)
    {
        // TODO
        return true;
    }

    void Emitter::block_epilogue(Block const &)
    {
        // TODO
    }

    void Emitter::gas_decrement_no_check(int64_t gas)
    {
        as_.sub(x86::qword_ptr(reg_context, context_offset_gas_remaining), gas);
    }

    void Emitter::gas_decrement_check_non_negative(int64_t gas)
    {
        gas_decrement_no_check(gas);
        as_.jb(out_of_gas_label_);
    }

    std::pair<StackElemRef, AvxRegReserv> Emitter::alloc_avx_reg()
    {
        auto [elem, reserv, offset] = stack_->alloc_avx_reg();
        if (offset.has_value()) {
            as_.vmovaps(
                stack_offset_to_mem(offset.value()),
                avx_reg_to_ymm(elem->avx_reg().value()));
        }
        return {elem, reserv};
    }

    AvxRegReserv Emitter::insert_avx_reg(StackElemRef elem)
    {
        auto [reserv, offset] = stack_->insert_avx_reg(elem);
        if (offset.has_value()) {
            as_.vmovaps(
                stack_offset_to_mem(offset.value()),
                avx_reg_to_ymm(elem->avx_reg().value()));
        }
        return reserv;
    }

    std::pair<StackElemRef, GeneralRegReserv> Emitter::alloc_general_reg()
    {
        auto [elem, reserv, offset] = stack_->alloc_general_reg();
        if (offset.has_value()) {
            mov_general_reg_to_mem(
                elem->general_reg().value(),
                stack_offset_to_mem(offset.value()));
        }
        return {elem, reserv};
    }

    GeneralRegReserv Emitter::insert_general_reg(StackElemRef elem)
    {
        auto [reserv, offset] = stack_->insert_general_reg(elem);
        if (offset.has_value()) {
            mov_general_reg_to_mem(
                elem->general_reg().value(),
                stack_offset_to_mem(offset.value()));
        }
        return reserv;
    }

    void Emitter::discharge_deferred_comparison()
    {
        if (!stack_->has_deferred_comparison()) {
            return;
        }
        auto dc = stack_->discharge_deferred_comparison();
        if (dc.stack_elem) {
            discharge_deferred_comparison(dc.stack_elem, dc.comparison);
        }
        if (dc.negated_stack_elem) {
            auto comp = negate_comparison(dc.comparison);
            discharge_deferred_comparison(dc.negated_stack_elem, comp);
        }
    }

    ////////// Private core emit functionality //////////

    void
    Emitter::discharge_deferred_comparison(StackElem *elem, Comparison comp)
    {
        auto [temp_reg, reserv] = alloc_avx_reg();
        auto y = avx_reg_to_ymm(temp_reg->avx_reg().value());
        auto m = stack_offset_to_mem(elem->stack_offset().value());
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

    Emitter::Gpq256 &Emitter::general_reg_to_gpq256(GeneralReg reg)
    {
        MONAD_COMPILER_DEBUG_ASSERT(reg.reg <= 2);
        return gpq256_regs_[reg.reg];
    }

    ////////// Move functionality //////////

    void Emitter::mov_stack_index_to_avx_reg(int32_t stack_index)
    {
        mov_stack_elem_to_avx_reg(stack_->get(stack_index));
    }

    void
    Emitter::mov_stack_index_to_general_reg_update_eflags(int32_t stack_index)
    {
        mov_stack_elem_to_general_reg<true>(stack_->get(stack_index));
    }

    void Emitter::mov_stack_index_to_stack_offset(int32_t stack_index)
    {
        mov_stack_elem_to_stack_offset(stack_->get(stack_index));
    }

    ////////// Private move functionality //////////

    template <bool assume_aligned>
    void Emitter::mov_literal_to_mem(Literal lit, asmjit::x86::Mem const &mem)
    {
        auto elem = stack_->alloc_literal(lit);
        mov_literal_to_avx_reg(elem);
        auto reg = elem->avx_reg().value();
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
        Literal lit, asmjit::x86::Mem const &mem)
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
        AvxReg const reg = elem->avx_reg().value();
        as_.vmovaps(avx_reg_to_ymm(reg), stack_offset_to_mem(offset));
        mov_avx_reg_to_unaligned_mem(reg, mem);
    }

    void Emitter::mov_stack_elem_to_unaligned_mem(
        StackElemRef elem, asmjit::x86::Mem const &mem)
    {
        if (elem->avx_reg()) {
            mov_avx_reg_to_unaligned_mem(elem->avx_reg().value(), mem);
        }
        else if (elem->general_reg()) {
            mov_general_reg_to_mem(elem->general_reg().value(), mem);
        }
        else if (elem->literal()) {
            mov_literal_to_unaligned_mem(elem->literal().value(), mem);
        }
        else {
            MONAD_COMPILER_ASSERT(elem->stack_offset().has_value());
            mov_stack_offset_to_unaligned_mem(
                elem->stack_offset().value(), mem);
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

    template <bool update_eflags>
    void Emitter::mov_stack_elem_to_general_reg(StackElemRef elem)
    {
        if (elem->general_reg()) {
            return;
        }
        if (elem->literal()) {
            mov_literal_to_general_reg<update_eflags>(std::move(elem));
        }
        else if (elem->stack_offset()) {
            mov_stack_offset_to_general_reg(std::move(elem));
        }
        else {
            MONAD_COMPILER_ASSERT(elem->avx_reg().has_value());
            mov_avx_reg_to_general_reg(std::move(elem));
        }
    }

    template <bool update_eflags>
    void
    Emitter::mov_stack_elem_to_general_reg(StackElemRef elem, int32_t preferred)
    {
        if (elem->general_reg()) {
            return;
        }
        if (elem->literal()) {
            mov_literal_to_general_reg<update_eflags>(std::move(elem));
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
        MONAD_COMPILER_DEBUG_ASSERT(elem->general_reg().has_value());
        mov_general_reg_to_stack_offset(elem, preferred);
        mov_stack_offset_to_avx_reg(elem);
    }

    void Emitter::mov_literal_to_avx_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->literal().has_value());
        auto avx_reserv = insert_avx_reg(elem);
        auto y = avx_reg_to_ymm(elem->avx_reg().value());
        auto lit = elem->literal().value();
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

    void Emitter::mov_stack_offset_to_avx_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->stack_offset().has_value());
        insert_avx_reg(elem);
        as_.vmovaps(
            avx_reg_to_ymm(elem->avx_reg().value()),
            stack_offset_to_mem(elem->stack_offset().value()));
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
        stack_->insert_stack_offset(elem, preferred);
        auto y = avx_reg_to_ymm(elem->avx_reg().value());
        as_.vmovaps(stack_offset_to_mem(elem->stack_offset().value()), y);
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
        stack_->insert_stack_offset(elem, preferred);
        mov_general_reg_to_mem(
            elem->general_reg().value(),
            stack_offset_to_mem(elem->stack_offset().value()));
    }

    void Emitter::mov_literal_to_stack_offset(StackElemRef elem)
    {
        int32_t const preferred = elem->preferred_stack_offset();
        mov_literal_to_stack_offset(std::move(elem), preferred);
    }

    void
    Emitter::mov_literal_to_stack_offset(StackElemRef elem, int32_t preferred)
    {
        stack_->insert_stack_offset(elem, preferred);
        mov_literal_to_mem<true>(
            elem->literal().value(),
            stack_offset_to_mem(elem->stack_offset().value()));
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

    template <bool update_eflags>
    void Emitter::mov_literal_to_general_reg(StackElemRef elem)
    {
        MONAD_COMPILER_DEBUG_ASSERT(elem->literal().has_value());
        insert_general_reg(elem);
        auto const &rs = general_reg_to_gpq256(elem->general_reg().value());
        auto lit = elem->literal().value();
        x86::Gpq const *zero_reg = nullptr;
        for (size_t i = 0; i < 4; ++i) {
            auto const &r = rs[i];
            if (lit.value[i] == 0) {
                if (zero_reg) {
                    as_.mov(r, *zero_reg);
                }
                else {
                    if constexpr (update_eflags) {
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
        x86::Mem temp{stack_offset_to_mem(elem->stack_offset().value())};
        for (auto r : general_reg_to_gpq256(elem->general_reg().value())) {
            as_.mov(r, temp);
            temp.addOffset(8);
        }
    }

    ////////// EVM instructions //////////

    // No discharge
    void Emitter::push(uint256_t const &x)
    {
        stack_->push_literal(x);
    }

    // No discharge
    void Emitter::pop()
    {
        stack_->pop();
    }

    // No discharge
    void Emitter::dup(uint8_t dup_ix)
    {
        MONAD_COMPILER_ASSERT(dup_ix > 0);
        stack_->dup(stack_->top_index() + 1 - static_cast<int32_t>(dup_ix));
    }

    // No discharge
    void Emitter::swap(uint8_t swap_ix)
    {
        MONAD_COMPILER_ASSERT(swap_ix > 0);
        stack_->swap(stack_->top_index() - static_cast<int32_t>(swap_ix));
    }

    // Discharge through `lt` overload
    void Emitter::lt()
    {
        auto left = stack_->pop();
        auto right = stack_->pop();
        lt(std::move(left), std::move(right));
    }

    // Discharge through `lt` overload
    void Emitter::gt()
    {
        auto left = stack_->pop();
        auto right = stack_->pop();
        lt(std::move(right), std::move(left));
    }

    // Discharge through `slt` overload
    void Emitter::slt()
    {
        auto left = stack_->pop();
        auto right = stack_->pop();
        slt(std::move(left), std::move(right));
    }

    // Discharge through `slt` overload
    void Emitter::sgt()
    {
        auto left = stack_->pop();
        auto right = stack_->pop();
        slt(std::move(right), std::move(left));
    }

    // Discharge
    void Emitter::sub()
    {
        auto pre_dst = stack_->pop();
        auto pre_src = stack_->pop();

        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(x - y);
            return;
        }

        discharge_deferred_comparison();

        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<false>(
            std::move(pre_dst), stack_->top_index() + 1, std::move(pre_src));

        auto dst_op = get_operand(dst, dst_loc);
        auto src_op = get_operand(src, src_loc);
        GENERAL_BIN_INSTR(sub, sbb)(dst_op, src_op);

        stack_->push(std::move(dst));
    }

    // Discharge
    void Emitter::add()
    {
        auto pre_dst = stack_->pop();
        auto pre_src = stack_->pop();

        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(x + y);
            return;
        }

        discharge_deferred_comparison();

        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<true>(
            std::move(pre_dst), stack_->top_index() + 1, std::move(pre_src));

        auto dst_op = get_operand(dst, dst_loc);
        auto src_op = get_operand(src, src_loc);
        GENERAL_BIN_INSTR(add, adc)(dst_op, src_op);

        stack_->push(std::move(dst));
    }

    // Discharge
    void Emitter::byte()
    {
        auto ix = stack_->pop();
        auto src = stack_->pop();

        if (ix->literal() && src->literal()) {
            auto const &i = ix->literal().value().value;
            auto const &x = src->literal().value().value;
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
            byte_literal_ix(
                ix->literal().value().value, src->stack_offset().value());
            return;
        }
        if (ix->general_reg()) {
            byte_general_reg_or_stack_offset_ix(
                std::move(ix), src->stack_offset().value());
            return;
        }
        if (!ix->stack_offset()) {
            mov_avx_reg_to_stack_offset(ix);
        }
        byte_general_reg_or_stack_offset_ix(
            std::move(ix), src->stack_offset().value());
    }

    // Discharge through `shift_by_stack_elem`
    void Emitter::shl()
    {
        auto shift = stack_->pop();
        auto value = stack_->pop();

        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal().value().value;
            auto const &x = value->literal().value().value;
            push(x << i);
            return;
        }

        shift_by_stack_elem<ShiftType::SHL>(std::move(shift), std::move(value));
    }

    // Discharge through `shift_by_stack_elem`
    void Emitter::shr()
    {
        auto shift = stack_->pop();
        auto value = stack_->pop();

        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal().value().value;
            auto const &x = value->literal().value().value;
            push(x >> i);
            return;
        }

        shift_by_stack_elem<ShiftType::SHR>(std::move(shift), std::move(value));
    }

    // Discharge through `shift_by_stack_elem`
    void Emitter::sar()
    {
        auto shift = stack_->pop();
        auto value = stack_->pop();

        if (shift->literal() && value->literal()) {
            auto const &i = shift->literal().value().value;
            auto const &x = value->literal().value().value;
            push(monad::utils::sar(i, x));
            return;
        }

        shift_by_stack_elem<ShiftType::SAR>(std::move(shift), std::move(value));
    }

    // Discharge
    void Emitter::and_()
    {
        auto pre_dst = stack_->pop();
        auto pre_src = stack_->pop();

        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(x & y);
            return;
        }

        discharge_deferred_comparison();

        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src));

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(and_, vpand)(dst, left_op, right_op);

        stack_->push(std::move(dst));
    }

    // Discharge
    void Emitter::or_()
    {
        auto pre_dst = stack_->pop();
        auto pre_src = stack_->pop();

        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(x | y);
            return;
        }

        discharge_deferred_comparison();

        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src));

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(or_, vpor)(dst, left_op, right_op);

        stack_->push(std::move(dst));
    }

    // Discharge
    void Emitter::xor_()
    {
        auto pre_dst = stack_->pop();
        auto pre_src = stack_->pop();

        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(x ^ y);
            return;
        }

        discharge_deferred_comparison();

        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src));

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(xor_, vpxor)(dst, left_op, right_op);

        stack_->push(std::move(dst));
    }

    // Discharge
    void Emitter::eq()
    {
        auto pre_dst = stack_->pop();
        auto pre_src = stack_->pop();

        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(x == y);
            return;
        }

        discharge_deferred_comparison();

        auto [dst, left, left_loc, right, right_loc] =
            get_avx_or_general_arguments_commutative(
                std::move(pre_dst), std::move(pre_src));

        auto left_op = get_operand(left, left_loc);
        auto right_op = get_operand(
            right, right_loc, std::holds_alternative<x86::Ymm>(left_op));
        AVX_OR_GENERAL_BIN_INSTR(xor_, vpxor)(dst, left_op, right_op);

        if (left_loc == LocationType::AvxReg) {
            x86::Ymm const &y = avx_reg_to_ymm(dst->avx_reg().value());
            as_.vptest(y, y);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(left_loc == LocationType::GeneralReg);
            Gpq256 const &gpq =
                general_reg_to_gpq256(dst->general_reg().value());
            for (size_t i = 0; i < 3; ++i) {
                as_.or_(gpq[i + 1], gpq[i]);
            }
        }
        stack_->push_deferred_comparison(Comparison::Equal);
    }

    // Discharge, except when top element is deferred comparison
    void Emitter::iszero()
    {
        if (stack_->negate_top_deferred_comparison()) {
            return;
        }
        auto elem = stack_->pop();
        if (elem->literal()) {
            push(elem->literal().value().value == 0);
            return;
        }
        discharge_deferred_comparison();
        auto [left, right, loc] = get_una_arguments(elem, std::nullopt);
        MONAD_COMPILER_DEBUG_ASSERT(left == right);
        if (loc == LocationType::AvxReg) {
            x86::Ymm const y = avx_reg_to_ymm(left->avx_reg().value());
            as_.vptest(y, y);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(loc == LocationType::GeneralReg);
            Gpq256 const &gpq =
                general_reg_to_gpq256(left->general_reg().value());
            for (size_t i = 0; i < 3; ++i) {
                as_.or_(gpq[i + 1], gpq[i]);
            }
        }
        stack_->push_deferred_comparison(Comparison::Equal);
    }

    // Discharge
    void Emitter::not_()
    {
        auto elem = stack_->pop();
        if (elem->literal()) {
            push(~elem->literal().value().value);
            return;
        }

        discharge_deferred_comparison();

        auto [left, right, loc] =
            get_una_arguments(elem, stack_->top_index() + 1);
        if (loc == LocationType::AvxReg) {
            x86::Ymm const y_left = avx_reg_to_ymm(left->avx_reg().value());
            x86::Ymm const y_right = avx_reg_to_ymm(right->avx_reg().value());
            auto lbl =
                append_literal(Literal{std::numeric_limits<uint256_t>::max()});
            as_.vpxor(y_left, y_right, x86::ptr(lbl));
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(loc == LocationType::GeneralReg);
            MONAD_COMPILER_DEBUG_ASSERT(left == right);
            Gpq256 const &gpq =
                general_reg_to_gpq256(left->general_reg().value());
            for (size_t i = 0; i < 4; ++i) {
                as_.not_(gpq[i]);
            }
        }
        stack_->push(std::move(left));
    }

    // No discharge
    void Emitter::address()
    {
        read_context_address(context_offset_recipient);
    }

    // No discharge
    void Emitter::caller()
    {
        read_context_address(context_offset_sender);
    }

    // No discharge
    void Emitter::callvalue()
    {
        read_context_word(context_offset_value);
    }

    // No discharge
    void Emitter::codesize(uint64_t cs)
    {
        stack_->push_literal(cs);
    }

    // No discharge
    void Emitter::stop()
    {
        status_code(runtime::StatusCode::Success);
        as_.jmp(epilogue_label_);
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
        as_.mov(reg_scratch, x86::ptr(x86::rsp, sp_offset_result_ptr));
        uint64_t const c = static_cast<uint64_t>(status);
        as_.mov(x86::qword_ptr(reg_scratch, result_offset_status), c);
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
        auto offset = stack_->pop();
        RegReserv const offset_avx_reserv{offset};
        auto size = stack_->pop();
        RegReserv const size_avx_reserv{size};
        status_code(status);
        mov_stack_elem_to_unaligned_mem(
            offset, qword_ptr(reg_scratch, result_offset_offset));
        mov_stack_elem_to_unaligned_mem(
            size, qword_ptr(reg_scratch, result_offset_size));
        as_.jmp(epilogue_label_);
    }

    void Emitter::read_context_address(int32_t offset)
    {
        x86::Mem m = x86::qword_ptr(reg_context, offset);
        auto [dst, _] = alloc_general_reg();
        Gpq256 const &gpq = general_reg_to_gpq256(dst->general_reg().value());
        as_.mov(gpq[0], m);
        m.addOffset(8);
        as_.mov(gpq[1], m);
        m.addOffset(8);
        m.setSize(4);
        as_.movzx(gpq[2], m);
        if (stack_->has_deferred_comparison()) {
            as_.mov(gpq[3], 0);
        }
        else {
            as_.xor_(gpq[3], gpq[3]);
        }
        stack_->push(std::move(dst));
    }

    void Emitter::read_context_word(int32_t offset)
    {
        x86::Mem const m = x86::qword_ptr(reg_context, offset);
        auto [dst, _] = alloc_avx_reg();
        as_.vmovups(avx_reg_to_ymm(dst->avx_reg().value()), m);
        stack_->push(std::move(dst));
    }

    void Emitter::lt(StackElemRef pre_dst, StackElemRef pre_src)
    {
        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(x < y);
            return;
        }
        discharge_deferred_comparison();
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<false>(
            std::move(pre_dst), std::nullopt, std::move(pre_src));
        cmp(std::move(dst), dst_loc, std::move(src), src_loc);
        stack_->push_deferred_comparison(Comparison::Below);
    }

    void Emitter::slt(StackElemRef pre_dst, StackElemRef pre_src)
    {
        if (pre_dst->literal() && pre_src->literal()) {
            auto const &x = pre_dst->literal().value().value;
            auto const &y = pre_src->literal().value().value;
            push(intx::slt(x, y));
            return;
        }
        discharge_deferred_comparison();
        auto [dst, dst_loc, src, src_loc] = get_general_dest_and_source<false>(
            std::move(pre_dst), std::nullopt, std::move(pre_src));
        cmp(std::move(dst), dst_loc, std::move(src), src_loc);
        stack_->push_deferred_comparison(Comparison::Less);
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
        Gpq256 const &gpq = general_reg_to_gpq256(dst->general_reg().value());

        as_.xor_(gpq[0], gpq[0]);
        as_.mov(gpq[1], gpq[0]);
        as_.mov(gpq[2], gpq[0]);
        as_.mov(gpq[3], gpq[0]);
        auto m = stack_offset_to_mem(src);
        m.addOffset(i);
        as_.mov(gpq[0].r8Lo(), m);

        stack_->push(std::move(dst));
    }

    void Emitter::byte_general_reg_or_stack_offset_ix(
        StackElemRef ix, StackOffset src)
    {
        auto [dst, dst_reserv] = alloc_general_reg();

        Gpq256 const &dst_gpq =
            general_reg_to_gpq256(dst->general_reg().value());

        as_.mov(dst_gpq[0], 31);
        as_.xor_(dst_gpq[1], dst_gpq[1]);
        as_.mov(dst_gpq[2], dst_gpq[1]);
        as_.mov(dst_gpq[3], dst_gpq[1]);
        if (ix->general_reg()) {
            Gpq256 const &ix_gpq =
                general_reg_to_gpq256(ix->general_reg().value());
            as_.sub(dst_gpq[0], ix_gpq[0]);
            as_.sbb(dst_gpq[1], ix_gpq[1]);
            as_.sbb(dst_gpq[2], ix_gpq[2]);
            as_.sbb(dst_gpq[3], ix_gpq[3]);
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(ix->stack_offset().has_value());
            x86::Mem m = stack_offset_to_mem(ix->stack_offset().value());
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

        stack_->push(std::move(dst));
    }

    template <Emitter::ShiftType shift_type>
    void Emitter::shift_by_stack_elem(StackElemRef shift, StackElemRef value)
    {
        RegReserv const ix_reserv{shift};
        RegReserv const src_reserv{value};

        discharge_deferred_comparison();

        if (shift->literal()) {
            shift_by_literal<shift_type>(
                shift->literal().value().value, std::move(value));
            return;
        }
        if (shift->general_reg()) {
            shift_by_general_reg_or_stack_offset<shift_type>(
                std::move(shift), std::move(value));
            return;
        }
        if (!shift->stack_offset()) {
            mov_avx_reg_to_stack_offset(shift);
        }
        shift_by_general_reg_or_stack_offset<shift_type>(
            std::move(shift), std::move(value));
    }

    template <Emitter::ShiftType shift_type>
    void Emitter::setup_shift_stack(StackElemRef value)
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
            if (value->general_reg()) {
                as_.mov(
                    x86::rax,
                    general_reg_to_gpq256(value->general_reg().value())[3]);
            }
            else {
                as_.mov(x86::rax, qword_ptr(x86::rsp, -40));
            }
            as_.sar(x86::rax, 63);
            as_.mov(qword_ptr(x86::rsp, -32), x86::rax);
            as_.mov(qword_ptr(x86::rsp, -24), x86::rax);
            as_.mov(qword_ptr(x86::rsp, -16), x86::rax);
            as_.mov(qword_ptr(x86::rsp, -8), x86::rax);
        }
    }

    template <Emitter::ShiftType shift_type>
    void Emitter::shift_by_literal(uint256_t shift, StackElemRef value)
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

        setup_shift_stack<shift_type>(value);

        int32_t const s = static_cast<int32_t>(shift[0]);
        int8_t const c = static_cast<int8_t>(s & 7);
        int32_t const d = s >> 3;

        auto [dst, dst_reserv] = alloc_general_reg();
        Gpq256 const &dst_gpq =
            general_reg_to_gpq256(dst->general_reg().value());

        if constexpr (shift_type == ShiftType::SHL) {
            as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, -8 - d));
            as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, -16 - d));
            as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, -24 - d));
            as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, -32 - d));
            as_.shld(dst_gpq[3], dst_gpq[2], c);
            as_.shld(dst_gpq[2], dst_gpq[1], c);
            as_.shld(dst_gpq[1], dst_gpq[0], c);
            as_.shl(dst_gpq[0], c);
        }
        else {
            as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, d - 40));
            as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, d - 48));
            as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, d - 56));
            as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, d - 64));
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

        stack_->push(std::move(dst));
    }

    template <Emitter::ShiftType shift_type>
    void Emitter::shift_by_general_reg_or_stack_offset(
        StackElemRef shift, StackElemRef value)
    {
        auto [dst, dst_reserv] = alloc_general_reg();
        Gpq256 &dst_gpq = general_reg_to_gpq256(dst->general_reg().value());
        Operand shift_op{dst_gpq};
        if (shift->general_reg()) {
            if (!shift->is_on_stack()) {
                shift_op = general_reg_to_gpq256(shift->general_reg().value());
            }
            else {
                Gpq256 const &shift_gpq =
                    general_reg_to_gpq256(shift->general_reg().value());
                for (size_t i = 0; i < 4; ++i) {
                    as_.mov(dst_gpq[i], shift_gpq[i]);
                }
            }
        }
        else {
            if (!shift->is_on_stack()) {
                shift_op = stack_offset_to_mem(shift->stack_offset().value());
            }
            else {
                x86::Mem shift_mem =
                    stack_offset_to_mem(shift->stack_offset().value());
                for (size_t i = 0; i < 4; ++i) {
                    as_.mov(dst_gpq[i], shift_mem);
                    shift_mem.addOffset(8);
                }
            }
        }

        if (std::holds_alternative<Gpq256>(shift_op)) {
            Gpq256 shift_gpq = std::get<Gpq256>(shift_op);
            as_.cmp(shift_gpq[0], 257);
            for (size_t i = 1; i < 4; ++i) {
                as_.sbb(shift_gpq[i], 0);
            }
        }
        else {
            x86::Mem shift_mem = std::get<x86::Mem>(shift_op);
            as_.cmp(shift_mem, 257);
            for (size_t i = 1; i < 4; ++i) {
                shift_mem.addOffset(8);
                as_.sbb(shift_mem, 0);
            }
        }

        auto shift_out_of_bounds_lbl = as_.newLabel();
        auto shift_resume_lbl = as_.newLabel();

        as_.jnb(shift_out_of_bounds_lbl);
        as_.bind(shift_resume_lbl);

        setup_shift_stack<shift_type>(value);

        bool preserve_cx = stack_->is_general_reg_on_stack(rcx_general_reg);
        if (preserve_cx &&
            dst->general_reg().value().reg != CALLEE_SAVE_GENERAL_REG_ID) {
            MONAD_COMPILER_DEBUG_ASSERT(
                dst->general_reg().value() != rcx_general_reg);
            as_.mov(dst_gpq[rcx_general_reg_index], x86::rcx);
            std::swap(
                gpq256_regs_[1][rcx_general_reg_index],
                gpq256_regs_[2][rcx_general_reg_index]);
            preserve_cx = false;
        }

        if (preserve_cx) {
            as_.mov(x86::ax, x86::cx);
        }

        bool const dst_has_rcx = dst_gpq[rcx_general_reg_index] == x86::rcx;

        uint8_t dst_i;
        if constexpr (shift_type == ShiftType::SHL) {
            dst_i = 0;
        }
        else {
            dst_i = 3;
        }

        if (dst_has_rcx) {
            std::swap(dst_gpq[dst_i], dst_gpq[rcx_general_reg_index]);
            rcx_general_reg_index = dst_i;
        }

        x86::Gpq const &scratch_reg =
            std::holds_alternative<Gpq256>(shift_op) && !shift->is_on_stack()
                ? std::get<Gpq256>(shift_op)[0]
                : x86::rax;
        x86::Gpq const &ireg = dst_has_rcx ? scratch_reg : dst_gpq[dst_i];

        if (std::holds_alternative<Gpq256>(shift_op)) {
            auto const &r = std::get<Gpq256>(shift_op)[0];
            if (r != ireg) {
                as_.mov(ireg, r);
            }
            if (r != x86::rcx && ireg != x86::rcx) {
                as_.mov(x86::cx, ireg.r16());
            }
        }
        else {
            as_.mov(ireg, std::get<x86::Mem>(shift_op));
            if (ireg != x86::rcx) {
                as_.mov(x86::cx, ireg.r16());
            }
        }

        as_.and_(x86::cl, 7);
        as_.shr(ireg.r16(), 3);

        if constexpr (shift_type == ShiftType::SHL) {
            as_.neg(ireg);
            as_.mov(dst_gpq[3], x86::qword_ptr(x86::rsp, ireg, 0, -8));
            as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, ireg, 0, -16));
            as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, ireg, 0, -24));
            as_.mov(ireg, x86::qword_ptr(x86::rsp, ireg, 0, -32));
            as_.shld(dst_gpq[3], dst_gpq[2], x86::cl);
            as_.shld(dst_gpq[2], dst_gpq[1], x86::cl);
            as_.shld(dst_gpq[1], ireg, x86::cl);
            if (dst_has_rcx) {
                as_.shlx(dst_gpq[0], ireg, x86::cl);
            }
            else {
                MONAD_COMPILER_DEBUG_ASSERT(ireg == dst_gpq[0]);
                as_.shl(dst_gpq[0], x86::cl);
            }
        }
        else {
            as_.mov(dst_gpq[0], x86::qword_ptr(x86::rsp, ireg, 0, -64));
            as_.mov(dst_gpq[1], x86::qword_ptr(x86::rsp, ireg, 0, -56));
            as_.mov(dst_gpq[2], x86::qword_ptr(x86::rsp, ireg, 0, -48));
            as_.mov(ireg, x86::qword_ptr(x86::rsp, ireg, 0, -40));
            as_.shrd(dst_gpq[0], dst_gpq[1], x86::cl);
            as_.shrd(dst_gpq[1], dst_gpq[2], x86::cl);
            as_.shrd(dst_gpq[2], ireg, x86::cl);
            if constexpr (shift_type == ShiftType::SHR) {
                if (dst_has_rcx) {
                    as_.shrx(dst_gpq[3], ireg, x86::cl);
                }
                else {
                    as_.shr(dst_gpq[3], x86::cl);
                }
            }
            else {
                static_assert(shift_type == ShiftType::SAR);
                if (dst_has_rcx) {
                    as_.sarx(dst_gpq[3], ireg, x86::cl);
                }
                else {
                    as_.sar(dst_gpq[3], x86::cl);
                }
            }
        }

        if (preserve_cx) {
            as_.mov(x86::cx, x86::ax);
        }

        shift_out_of_bounds_handlers_.emplace_back(
            shift_out_of_bounds_lbl, shift_op, shift_resume_lbl);

        stack_->push(std::move(dst));
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
                    is_literal_bounded(dst->literal().value())) {
                    std::swap(dst, src);
                }
            }
        }

        if (dst.get() == src.get()) {
            if (!dst->general_reg()) {
                mov_stack_elem_to_general_reg<true>(dst);
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
            if (src->literal() && is_literal_bounded(src->literal().value())) {
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
                mov_literal_to_general_reg<true>(dst);
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
                    mov_avx_reg_to_stack_offset(dst, dst_ix.value());
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
        if (src->literal() && is_literal_bounded(src->literal().value())) {
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
            mov_literal_to_general_reg<true>(src);
            return {
                std::move(dst),
                LocationType::StackOffset,
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

    template <bool commutative>
    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::get_general_dest_and_source(
        StackElemRef dst_in, std::optional<int32_t> dst_ix, StackElemRef src_in)
    {
        auto [dst, dst_loc, src, src_loc] =
            prepare_general_dest_and_source<commutative>(
                std::move(dst_in), dst_ix, std::move(src_in));
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if (dst_loc == LocationType::GeneralReg) {
            if (dst->is_on_stack() && !dst->stack_offset() && !dst->literal() &&
                !dst->avx_reg()) {
                mov_general_reg_to_stack_offset(dst);
            }
            auto new_dst = stack_->release_general_reg(dst);
            if (dst.get() == src.get()) {
                return {new_dst, dst_loc, new_dst, src_loc};
            }
            else {
                return {std::move(new_dst), dst_loc, std::move(src), src_loc};
            }
        }
        MONAD_COMPILER_DEBUG_ASSERT(dst.get() != src.get());
        MONAD_COMPILER_DEBUG_ASSERT(dst_loc == LocationType::StackOffset);
        if (dst->is_on_stack() && !dst->general_reg() && !dst->literal() &&
            !dst->avx_reg()) {
            mov_stack_offset_to_avx_reg(dst);
        }
        return {
            stack_->release_stack_offset(std::move(dst)),
            dst_loc,
            std::move(src),
            src_loc};
    }

    Emitter::Operand Emitter::get_operand(
        StackElemRef elem, LocationType loc, bool always_append_literal)
    {
        switch (loc) {
        case LocationType::StackOffset:
            return stack_offset_to_mem(elem->stack_offset().value());
        case LocationType::GeneralReg:
            return general_reg_to_gpq256(elem->general_reg().value());
        case LocationType::Literal:
            if (!always_append_literal &&
                is_literal_bounded(elem->literal().value())) {
                return literal_to_imm256(elem->literal().value());
            }
            else {
                auto lbl = append_literal(elem->literal().value());
                return x86::qword_ptr(lbl);
            }
        case LocationType::AvxReg:
            return avx_reg_to_ymm(elem->avx_reg().value());
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
                    [](auto const &) { std::unreachable(); },
                },
                src_op);
        }
    }

    // Note that if dst_ix is null, then it is assumed that the unary avx
    // instruction will not mutate the destination register.
    std::tuple<StackElemRef, StackElemRef, Emitter::LocationType>
    Emitter::get_una_arguments(StackElemRef dst, std::optional<int32_t> dst_ix)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!dst->literal());
        RegReserv const dst_reserv{dst};
        if (!dst->avx_reg()) {
            if (dst->stack_offset()) {
                mov_stack_offset_to_avx_reg(dst);
            }
            else if (dst->is_on_stack()) {
                mov_general_reg_to_avx_reg(dst);
            }
        }
        if (dst->avx_reg()) {
            if (!dst_ix || !dst->is_on_stack()) {
                return {dst, dst, LocationType::AvxReg};
            }
            auto [elem, _] = alloc_avx_reg();
            return {elem, dst, LocationType::AvxReg};
        }
        MONAD_COMPILER_DEBUG_ASSERT(dst->general_reg() && !dst->is_on_stack());
        return {dst, dst, LocationType::GeneralReg};
    }

    std::tuple<
        StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::prepare_avx_or_general_arguments_commutative(
        StackElemRef dst, StackElemRef src)
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
            if (dst->general_reg() && !dst->is_on_stack()) {
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
            [this, &priority_1](StackElemRef &d, StackElemRef &s) -> OptResult {
            if (d->stack_offset()) {
                if (s->stack_offset()) {
                    if (d->is_on_stack()) {
                        mov_stack_offset_to_avx_reg(s);
                        return priority_1(s, d);
                    }
                    mov_stack_offset_to_avx_reg(d);
                    return priority_1(d, s);
                }
                if (s->literal()) {
                    if (d->is_on_stack()) {
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

        auto priority_3 = [](StackElemRef &d, StackElemRef &s) -> OptResult {
            if (!d->general_reg()) {
                return std::nullopt;
            }
            if (d->is_on_stack() && !d->literal() && !d->stack_offset() &&
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
        if (src->is_on_stack()) {
            result = priority_4(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (dst->is_on_stack()) {
            result = priority_4(src, dst);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!src->is_on_stack()) {
            result = priority_4(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!dst->is_on_stack()) {
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
        if (src->is_on_stack()) {
            result = priority_5(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (dst->is_on_stack()) {
            result = priority_5(src, dst);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!src->is_on_stack()) {
            result = priority_5(dst, src);
            if (result.has_value()) {
                return *result;
            }
        }
        if (!dst->is_on_stack()) {
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

    std::tuple<
        StackElemRef, StackElemRef, Emitter::LocationType, StackElemRef,
        Emitter::LocationType>
    Emitter::get_avx_or_general_arguments_commutative(
        StackElemRef dst_in, StackElemRef src_in)
    {
        auto [dst, dst_loc, src, src_loc] =
            prepare_avx_or_general_arguments_commutative(
                std::move(dst_in), std::move(src_in));
        RegReserv const dst_reserv{dst};
        RegReserv const src_reserv{src};

        if (dst_loc == LocationType::GeneralReg) {
            MONAD_COMPILER_DEBUG_ASSERT(
                !dst->is_on_stack() || dst->stack_offset().has_value() ||
                dst->literal().has_value() || dst->avx_reg().has_value());
            MONAD_COMPILER_DEBUG_ASSERT(dst.get() != src.get());
            auto new_dst = stack_->release_general_reg(dst);
            return {new_dst, new_dst, dst_loc, std::move(src), src_loc};
        }
        else {
            MONAD_COMPILER_DEBUG_ASSERT(dst_loc == LocationType::AvxReg);
            StackElemRef const new_dst;
            if (dst->is_on_stack()) {
                if (!src->is_on_stack() && src_loc == LocationType::AvxReg) {
                    auto n = stack_->release_avx_reg(src);
                    return {n, std::move(dst), dst_loc, n, src_loc};
                }
                else {
                    auto [n, _] = alloc_avx_reg();
                    return {
                        n, std::move(dst), dst_loc, std::move(src), src_loc};
                }
            }
            auto n = stack_->release_avx_reg(dst);
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
