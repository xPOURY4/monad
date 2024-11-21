#pragma once

#include "compiler/ir/local_stacks.h"
#include <compiler/ir/x86.h>
#include <compiler/ir/x86/virtual_stack.h>
#include <runtime/types.h>

#include <asmjit/x86.h>
#include <asmjit/x86/x86assembler.h>

using namespace monad::compiler::local_stacks;

namespace monad::compiler::native
{
    class Emitter
    {
    public:
        struct Error : std::runtime_error
        {
            Error(std::string const &msg);
            Error(char const *msg);
        };

        struct EmitErrorHandler : asmjit::ErrorHandler
        {
            void handleError(
                asmjit::Error, char const *message,
                asmjit::BaseEmitter *) override;
        };

        enum class LocationType
        {
            StackOffset,
            Literal,
            AvxReg,
            GeneralReg
        };

        static char const *location_type_to_string(LocationType);

        using Gpq256 = std::array<asmjit::x86::Gpq, 4>;
        using Imm256 = std::array<asmjit::Imm, 4>;

        using Operand =
            std::variant<Gpq256, Imm256, asmjit::x86::Ymm, asmjit::x86::Mem>;

        template <typename L, typename R>
        using BinInstr = std::array<
            asmjit::Error (asmjit::x86::Assembler::*)(L const &, R const &), 4>;

        ////////// Initialization and de-initialization //////////

        Emitter(
            asmjit::JitRuntime const &, char const *debug_log_file = nullptr);

        ~Emitter();

        entrypoint_t finish_contract(asmjit::JitRuntime &);

        ////////// Core emit functionality //////////

        void switch_stack(Stack *);
        bool block_prologue(Block const &);
        void block_epilogue(Block const &);
        void gas_decrement_no_check(int64_t);
        void gas_decrement_check_non_negative(int64_t);
        std::pair<StackElemRef, AvxRegReserv> alloc_avx_reg();
        void discharge_deferred_comparison();

        ////////// Move functionality //////////

        void mov_stack_index_to_avx_reg(int32_t stack_index);
        void mov_stack_index_to_general_reg_update_eflags(int32_t stack_index);
        void mov_stack_index_to_stack_offset(int32_t stack_index);

        ////////// EVM instructions //////////

        void push(uint256_t const &);
        void pop();
        void dup(uint8_t dup_index);
        void swap(uint8_t swap_index);
        void lt();
        void gt();
        void slt();
        void sgt();
        void sub();
        void add();
        void stop();
        void return_();
        void revert();

    private:
        ////////// Private initialization and de-initialization //////////

        asmjit::CodeHolder *
        init_code_holder(asmjit::JitRuntime const &, char const *);
        void contract_prologue();
        void contract_epilogue();

        ////////// Private Core emit functionality //////////

        asmjit::Label const &append_literal(Literal);

        ////////// Private move functionality //////////

        template <bool assume_aligned>
        void mov_literal_to_mem(Literal, asmjit::x86::Mem const &);

        void mov_general_reg_to_mem(GeneralReg, asmjit::x86::Mem const &);
        void mov_literal_to_unaligned_mem(Literal, asmjit::x86::Mem const &);
        void mov_avx_reg_to_unaligned_mem(AvxReg, asmjit::x86::Mem const &);
        void mov_stack_offset_to_unaligned_mem(
            StackOffset, asmjit::x86::Mem const &);
        void
        mov_stack_elem_to_unaligned_mem(StackElemRef, asmjit::x86::Mem const &);

        void mov_stack_elem_to_avx_reg(StackElemRef);
        void mov_stack_elem_to_avx_reg(StackElemRef, int32_t preferred_offset);
        void mov_stack_elem_to_general_reg_update_eflags(StackElemRef);
        void mov_stack_elem_to_general_reg_update_eflags(
            StackElemRef, int32_t preferred_offset);
        void mov_stack_elem_to_stack_offset(StackElemRef);
        void
        mov_stack_elem_to_stack_offset(StackElemRef, int32_t preferred_offset);

        void mov_general_reg_to_avx_reg(StackElemRef);
        void mov_general_reg_to_avx_reg(StackElemRef, int32_t preferred_offset);
        void mov_literal_to_avx_reg(StackElemRef);
        void mov_stack_offset_to_avx_reg(StackElemRef);

        void mov_avx_reg_to_stack_offset(StackElemRef);
        void
        mov_avx_reg_to_stack_offset(StackElemRef, int32_t preferred_offset);
        void mov_general_reg_to_stack_offset(StackElemRef);
        void
        mov_general_reg_to_stack_offset(StackElemRef, int32_t preferred_offset);
        void mov_literal_to_stack_offset(StackElemRef);
        void
        mov_literal_to_stack_offset(StackElemRef, int32_t preferred_offset);

        void mov_avx_reg_to_general_reg_update_eflags(StackElemRef);
        void mov_avx_reg_to_general_reg_update_eflags(
            StackElemRef, int32_t preferred_offset);
        void mov_literal_to_general_reg_update_eflags(StackElemRef);
        void mov_stack_offset_to_general_reg_update_eflags(StackElemRef);

        ////////// Private EVM instruction utilities //////////

        void status_code(monad::runtime::StatusCode);
        void error_block(asmjit::Label &, monad::runtime::StatusCode);
        void return_with_status_code(monad::runtime::StatusCode);

        void lt(StackElemRef dst, StackElemRef src);
        void slt(StackElemRef dst, StackElemRef src);
        void
        cmp(StackElemRef dst, LocationType, StackElemRef src, LocationType);

        std::pair<LocationType, LocationType>
        prepare_general_dest_and_source_with_regs_reserved(
            StackElemRef dst, std::optional<int32_t> dst_stack_index,
            StackElemRef src);

        template <bool commutative>
        std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>
        get_general_dest_and_source(
            StackElemRef dst, std::optional<int32_t> dst_stack_index,
            StackElemRef src);

        Operand get_operand(StackElemRef, LocationType);

        template <
            BinInstr<asmjit::x86::Gp, asmjit::x86::Gp> GG,
            BinInstr<asmjit::x86::Gp, asmjit::x86::Mem> GM,
            BinInstr<asmjit::x86::Gp, asmjit::Imm> GI,
            BinInstr<asmjit::x86::Mem, asmjit::x86::Gp> MG,
            BinInstr<asmjit::x86::Mem, asmjit::Imm> MI>
        void general_bin_instr(Operand const &dst_op, Operand const &src_op);

        ////////// Fields //////////

        // Order of fields is significant.
        EmitErrorHandler error_handler_;
        asmjit::CodeHolder code_holder_;
        asmjit::FileLogger debug_logger_;
        asmjit::x86::Assembler as_;
        asmjit::Label epilogue_label_;
        asmjit::Label out_of_gas_label_;
        asmjit::Label overflow_label_;
        asmjit::Label underflow_label_;
        Stack *stack_;
        std::vector<std::pair<asmjit::Label, Literal>> literals_;
    };
}
