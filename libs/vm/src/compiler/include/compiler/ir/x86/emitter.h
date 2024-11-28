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
        using GeneralBinInstr = std::array<
            asmjit::Error (asmjit::x86::Assembler::*)(L const &, R const &), 4>;

        template <typename R>
        using AvxBinInstr = asmjit::Error (
            asmjit::x86::EmitterExplicitT<asmjit::x86::Assembler>::*)(
            asmjit::x86::Vec const &, asmjit::x86::Vec const &, R const &);

        ////////// Initialization and de-initialization //////////

        Emitter(
            asmjit::JitRuntime const &, char const *debug_log_file = nullptr);

        ~Emitter();

        entrypoint_t finish_contract(asmjit::JitRuntime &);

        ////////// Core emit functionality //////////

        Stack &get_stack();
        void begin_stack(Block const &);
        bool block_prologue(Block const &);
        void block_epilogue(Block const &);
        void gas_decrement_no_check(int64_t);
        void gas_decrement_check_non_negative(int64_t);
        std::pair<StackElemRef, AvxRegReserv> alloc_avx_reg();
        std::pair<StackElemRef, GeneralRegReserv> alloc_general_reg();
        void discharge_deferred_comparison(); // Leaves eflags unchanged

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
        void byte();
        void shl();
        void shr();
        void sar();

        void and_();
        void or_();
        void xor_();
        void eq();

        void iszero();
        void not_();

        void address();
        void codesize(uint64_t contract_bytecode_size);

        void stop();
        void return_();
        void revert();

    private:
        ////////// Private initialization and de-initialization //////////

        asmjit::CodeHolder *
        init_code_holder(asmjit::JitRuntime const &, char const *);
        void contract_prologue();
        void contract_epilogue();

        ////////// Private core emit functionality //////////

        void discharge_deferred_comparison(StackElem *, Comparison);

        asmjit::Label const &append_literal(Literal);

        Gpq256 &general_reg_to_gpq256(GeneralReg);

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
        template <bool update_eflags>
        void mov_stack_elem_to_general_reg(StackElemRef);
        template <bool update_eflags>
        void
        mov_stack_elem_to_general_reg(StackElemRef, int32_t preferred_offset);
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

        void mov_avx_reg_to_general_reg(StackElemRef);
        void mov_avx_reg_to_general_reg(StackElemRef, int32_t preferred_offset);
        template <bool update_eflags>
        void mov_literal_to_general_reg(StackElemRef);
        void mov_stack_offset_to_general_reg(StackElemRef);

        ////////// Private EVM instruction utilities //////////

        void status_code(monad::runtime::StatusCode);
        void error_block(asmjit::Label &, monad::runtime::StatusCode);
        void return_with_status_code(monad::runtime::StatusCode);

        void lt(StackElemRef dst, StackElemRef src);
        void slt(StackElemRef dst, StackElemRef src);
        void
        cmp(StackElemRef dst, LocationType, StackElemRef src, LocationType);

        void byte_literal_ix(uint256_t const &ix, StackOffset src);
        void
        byte_general_reg_or_stack_offset_ix(StackElemRef ix, StackOffset src);

        enum class ShiftType
        {
            SHL,
            SHR,
            SAR
        };

        template <ShiftType shift_type>
        void shift_by_stack_elem(StackElemRef shift, StackElemRef);

        template <ShiftType shift_type>
        void setup_shift_stack(StackElemRef);

        template <ShiftType shift_type>
        void shift_by_literal(uint256_t shift, StackElemRef);

        template <ShiftType shift_type>
        void
        shift_by_general_reg_or_stack_offset(StackElemRef shift, StackElemRef);

        template <bool commutative>
        std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>
        prepare_general_dest_and_source(
            StackElemRef dst, std::optional<int32_t> dst_stack_index,
            StackElemRef src);

        template <bool commutative>
        std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>
        get_general_dest_and_source(
            StackElemRef dst, std::optional<int32_t> dst_stack_index,
            StackElemRef src);

        Operand get_operand(
            StackElemRef, LocationType, bool always_append_literal = false);

        template <
            GeneralBinInstr<asmjit::x86::Gp, asmjit::x86::Gp> GG,
            GeneralBinInstr<asmjit::x86::Gp, asmjit::x86::Mem> GM,
            GeneralBinInstr<asmjit::x86::Gp, asmjit::Imm> GI,
            GeneralBinInstr<asmjit::x86::Mem, asmjit::x86::Gp> MG,
            GeneralBinInstr<asmjit::x86::Mem, asmjit::Imm> MI>
        void general_bin_instr(Operand const &dst_op, Operand const &src_op);

        std::tuple<StackElemRef, StackElemRef, LocationType> get_una_arguments(
            StackElemRef dst, std::optional<int32_t> dst_stack_index);

        std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>
        prepare_avx_or_general_arguments_commutative(
            StackElemRef dst, StackElemRef src);

        std::tuple<
            StackElemRef, StackElemRef, LocationType, StackElemRef,
            LocationType>
        get_avx_or_general_arguments_commutative(
            StackElemRef dst, StackElemRef src);

        template <
            GeneralBinInstr<asmjit::x86::Gp, asmjit::x86::Gp> GG,
            GeneralBinInstr<asmjit::x86::Gp, asmjit::x86::Mem> GM,
            GeneralBinInstr<asmjit::x86::Gp, asmjit::Imm> GI,
            GeneralBinInstr<asmjit::x86::Mem, asmjit::x86::Gp> MG,
            GeneralBinInstr<asmjit::x86::Mem, asmjit::Imm> MI,
            AvxBinInstr<asmjit::x86::Vec> VV, AvxBinInstr<asmjit::x86::Mem> VM>
        void avx_or_general_bin_instr(
            StackElemRef dst, Operand const &left, Operand const &right);

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
        std::unique_ptr<Stack> stack_;
        std::array<Gpq256, 3> gpq256_regs_;
        GeneralReg rcx_general_reg;
        uint8_t rcx_general_reg_index;
        std::vector<std::pair<asmjit::Label, Literal>> literals_;
        std::vector<std::tuple<asmjit::Label, Gpq256, asmjit::Label>>
            byte_out_of_bounds_handlers_;
        std::vector<std::tuple<asmjit::Label, Operand, asmjit::Label>>
            shift_out_of_bounds_handlers_;
    };
}
