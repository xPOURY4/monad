#pragma once

#include <monad/compiler/ir/basic_blocks.hpp>
#include <monad/compiler/ir/x86.hpp>
#include <monad/compiler/ir/x86/virtual_stack.hpp>
#include <monad/runtime/detail.hpp>
#include <monad/runtime/types.hpp>

#include <asmjit/x86.h>
#include <asmjit/x86/x86assembler.h>

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

        static constexpr auto MAX_RUNTIME_ARGS = 12;

        class RuntimeImpl
        {
        public:
            template <typename... Args>
            RuntimeImpl(
                Emitter *e, int32_t remaining_base_gas, void (*f)(Args...))
                : em_{e}
                , remaining_base_gas_{remaining_base_gas}
                , runtime_fun_{reinterpret_cast<void *>(f)}
                , arg_count_{sizeof...(Args)}
                , context_arg_{monad::runtime::detail::context_arg_t<
                      Args...>::context_arg}
                , result_arg_{monad::runtime::detail::result_arg_t<
                      Args...>::result_arg}
                , remaining_gas_arg_{
                      monad::runtime::detail::remaining_gas_arg_t<
                          Args...>::remaining_gas_arg}
            {
            }

            RuntimeImpl &pass(StackElemRef &&);

            void call_impl();

            size_t implicit_arg_count();
            size_t explicit_arg_count();

        protected:
            using RuntimeArg =
                std::variant<asmjit::x86::Gpq, asmjit::Imm, asmjit::x86::Mem>;

            void mov_arg(size_t arg_index, RuntimeArg &&arg);
            void mov_reg_arg(asmjit::x86::Gpq const &, RuntimeArg &&arg);
            void mov_stack_arg(int32_t sp_offset, RuntimeArg &&arg);

            Emitter *em_;
            std::vector<StackElemRef> explicit_args_;
            int32_t remaining_base_gas_;
            void *runtime_fun_;
            size_t arg_count_;
            std::optional<size_t> context_arg_;
            std::optional<size_t> result_arg_;
            std::optional<size_t> remaining_gas_arg_;
        };

        template <typename... Args>
        class Runtime : public RuntimeImpl
        {
        public:
            Runtime(Emitter *e, int32_t remaining_base_gas, void (*f)(Args...))
                : RuntimeImpl(e, remaining_base_gas, f)
            {
                static_assert(
                    monad::runtime::detail::uses_context_v<Args...> ||
                    monad::runtime::detail::uses_remaining_gas_v<Args...>);
            }

            void call()
            {
                em_->call_runtime_impl(*this);
            }
        };

        template <typename... Args>
        class Runtime<uint256_t *, Args...> : public RuntimeImpl
        {
        public:
            Runtime(
                Emitter *e, int32_t remaining_base_gas,
                void (*f)(uint256_t *, Args...))
                : RuntimeImpl(e, remaining_base_gas, f)
            {
                static_assert(monad::runtime::detail::is_result_v<uint256_t *>);
                static_assert(!monad::runtime::detail::uses_context_v<Args...>);
                static_assert(
                    !monad::runtime::detail::uses_remaining_gas_v<Args...>);
            }

            uint256_t static_call(Args... args)
            {
                uint256_t result;
                auto f = reinterpret_cast<void (*)(uint256_t *, Args...)>(
                    runtime_fun_);
                f(&result, args...);
                return result;
            }

            void call()
            {
                em_->call_runtime_pure(
                    *this, std::index_sequence_for<Args...>{});
            }
        };

        friend class RuntimeImpl;
        template <typename... Args>
        friend class Runtime;

        ////////// Initialization and de-initialization //////////

        Emitter(
            asmjit::JitRuntime const &, uint64_t bytecode_size,
            char const *debug_log_file = nullptr);

        ~Emitter();

        entrypoint_t finish_contract(asmjit::JitRuntime &);

        ////////// Debug functionality //////////

        bool is_debug_enabled();
        void runtime_print_gas_remaining(std::string const &msg);
        void runtime_print_input_stack(std::string const &msg);
        void runtime_print_top2(std::string const &msg);
        void breakpoint();
        void asm_comment(std::string const &msg);

        ////////// Core emit functionality //////////

        Stack &get_stack();
        void add_jump_dest(byte_offset);
        [[nodiscard]]
        bool begin_new_block(basic_blocks::Block const &);
        void gas_decrement_no_check(int32_t);
        void gas_decrement_check_non_negative(int32_t);
        void spill_all_caller_save_regs();
        void spill_all_caller_save_general_regs();
        void spill_all_avx_regs();
        std::pair<StackElemRef, AvxRegReserv> alloc_avx_reg();
        AvxRegReserv insert_avx_reg(StackElemRef);
        std::pair<StackElemRef, GeneralRegReserv> alloc_general_reg();
        GeneralRegReserv insert_general_reg(StackElemRef);
        void discharge_deferred_comparison(); // Leaves eflags unchanged

        ////////// Move functionality //////////

        void mov_stack_index_to_avx_reg(int32_t stack_index);
        void mov_stack_index_to_general_reg(int32_t stack_index);
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
        void signextend();
        void shl();
        void shr();
        void sar();

        void and_();
        void or_();
        void xor_();
        void eq();

        void iszero();
        void not_();

        void gas(int32_t remaining_base_gas);

        void address();
        void caller();
        void callvalue();
        void calldatasize();
        void returndatasize();
        void msize();
        void codesize();
        void origin();
        void gasprice();
        void gaslimit();
        void coinbase();
        void timestamp();
        void number();
        void prevrandao();
        void chainid();
        void basefee();
        void blobbasefee();

        // Revision dependent instructions
        template <evmc_revision rev>
        void mul(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::mul<rev>);
        }

        template <evmc_revision rev>
        void udiv(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::udiv<rev>);
        }

        template <evmc_revision rev>
        void sdiv(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::sdiv<rev>);
        }

        template <evmc_revision rev>
        void umod(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::umod<rev>);
        }

        template <evmc_revision rev>
        void smod(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::smod<rev>);
        }

        template <evmc_revision rev>
        void addmod(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::addmod<rev>);
        }

        template <evmc_revision rev>
        void mulmod(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::mulmod<rev>);
        }

        template <evmc_revision rev>
        void exp(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::exp<rev>);
        }

        template <evmc_revision rev>
        void sha3(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::sha3<rev>);
        }

        template <evmc_revision rev>
        void balance(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::balance<rev>);
        }

        template <evmc_revision rev>
        void calldataload(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::calldataload<rev>);
        }

        template <evmc_revision rev>
        void calldatacopy(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::calldatacopy<rev>);
        }

        template <evmc_revision rev>
        void codecopy(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::codecopy<rev>);
        }

        template <evmc_revision rev>
        void extcodesize(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::extcodesize<rev>);
        }

        template <evmc_revision rev>
        void extcodecopy(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::extcodecopy<rev>);
        }

        template <evmc_revision rev>
        void returndatacopy(int32_t remaining_base_gas)
        {
            call_runtime(
                remaining_base_gas, monad::runtime::returndatacopy<rev>);
        }

        template <evmc_revision rev>
        void extcodehash(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::extcodehash<rev>);
        }

        template <evmc_revision rev>
        void blockhash(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::blockhash<rev>);
        }

        template <evmc_revision rev>
        void selfbalance(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::selfbalance<rev>);
        }

        template <evmc_revision rev>
        void blobhash(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::blobhash<rev>);
        }

        template <evmc_revision rev>
        void mload(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::mload<rev>);
        }

        template <evmc_revision rev>
        void mstore(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::mstore<rev>);
        }

        template <evmc_revision rev>
        void mstore8(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::mstore8<rev>);
        }

        template <evmc_revision rev>
        void sload(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::sload<rev>);
        }

        template <evmc_revision rev>
        void sstore(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::sstore<rev>);
        }

        template <evmc_revision rev>
        void tload(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::tload<rev>);
        }

        template <evmc_revision rev>
        void tstore(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::tstore<rev>);
        }

        template <evmc_revision rev>
        void mcopy(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::mcopy<rev>);
        }

        template <evmc_revision rev>
        void log0(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::log0<rev>);
        }

        template <evmc_revision rev>
        void log1(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::log1<rev>);
        }

        template <evmc_revision rev>
        void log2(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::log2<rev>);
        }

        template <evmc_revision rev>
        void log3(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::log3<rev>);
        }

        template <evmc_revision rev>
        void log4(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::log4<rev>);
        }

        template <evmc_revision rev>
        void create(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::create<rev>);
        }

        template <evmc_revision rev>
        void call(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::call<rev>);
        }

        template <evmc_revision rev>
        void callcode(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::callcode<rev>);
        }

        template <evmc_revision rev>
        void delegatecall(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::delegatecall<rev>);
        }

        template <evmc_revision rev>
        void create2(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::create2<rev>);
        }

        template <evmc_revision rev>
        void staticcall(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::staticcall<rev>);
        }

        template <evmc_revision rev>
        void selfdestruct(int32_t remaining_base_gas)
        {
            call_runtime(remaining_base_gas, monad::runtime::selfdestruct<rev>);
        }

        // TODO(dhil): We'd like this function to be private. Though,
        // currently we use this function in `emitter_tests.cpp` to
        // call two handcrafted test functions.
        template <typename... Args>
        void call_runtime(int32_t remaining_base_gas, void (*f)(Args...))
        {
            Runtime<Args...>(this, remaining_base_gas, f).call();
        }

        // Terminators invalidate emitter until `begin_new_block` is called.
        void jump();
        void jumpi();
        void fallthrough();
        void stop();
        void invalid_instruction();
        void return_();
        void revert();

    private:
        ////////// Private initialization and de-initialization //////////

        asmjit::CodeHolder *
        init_code_holder(asmjit::JitRuntime const &, char const *);
        void contract_prologue();
        void contract_epilogue();

        ////////// Private debug functionality //////////

        void unsafe_asm_comment(std::string const &msg);

        ////////// Private core emit functionality //////////

        template <typename... LiveSet, size_t... Is>
        bool is_live(
            StackElemRef, std::tuple<LiveSet...> const &,
            std::index_sequence<Is...>);

        template <typename... LiveSet>
        bool is_live(StackElemRef, std::tuple<LiveSet...> const &);

        bool block_prologue(basic_blocks::Block const &);
        int32_t block_epilogue();
        void write_to_final_stack_offsets();

        void discharge_deferred_comparison(DeferredComparison const &);
        void discharge_deferred_comparison(StackElem *, Comparison);

        asmjit::Label const &append_literal(Literal);
        asmjit::Label const &append_external_function(void *);

        Gpq256 &general_reg_to_gpq256(GeneralReg);

        ////////// Private move functionality //////////

        template <bool assume_aligned>
        void mov_literal_to_mem(Literal const &, asmjit::x86::Mem const &);

        void mov_general_reg_to_mem(GeneralReg, asmjit::x86::Mem const &);
        void
        mov_literal_to_unaligned_mem(Literal const &, asmjit::x86::Mem const &);
        void mov_avx_reg_to_unaligned_mem(AvxReg, asmjit::x86::Mem const &);
        void mov_stack_offset_to_unaligned_mem(
            StackOffset, asmjit::x86::Mem const &);
        void
        mov_stack_elem_to_unaligned_mem(StackElemRef, asmjit::x86::Mem const &);

        void mov_general_reg_to_gpq256(GeneralReg, Gpq256 const &);
        void mov_literal_to_gpq256(Literal const &, Gpq256 const &);
        void mov_stack_offset_to_gpq256(StackOffset, Gpq256 const &);
        void mov_stack_elem_to_gpq256(StackElemRef, Gpq256 const &);

        void mov_literal_to_ymm(Literal const &, asmjit::x86::Ymm const &);

        void mov_stack_elem_to_avx_reg(StackElemRef);
        void mov_stack_elem_to_avx_reg(StackElemRef, int32_t preferred_offset);
        void mov_stack_elem_to_general_reg(StackElemRef);
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
        void mov_literal_to_general_reg(StackElemRef);
        void mov_stack_offset_to_general_reg(StackElemRef);

        ////////// Private EVM instruction utilities //////////

        void call_runtime_impl(RuntimeImpl &rt);

        template <typename... Args, size_t... Is>
        void call_runtime_pure(
            Runtime<uint256_t *, Args...> &rt, std::index_sequence<Is...>)
        {
            std::array<StackElemRef, sizeof...(Args)> elems;
            ((std::get<Is>(elems) = stack_.pop()), ...);
            if ((... && std::get<Is>(elems)->literal())) {
                auto args =
                    std::make_tuple(&std::get<Is>(elems)->literal()->value...);
                push(std::apply(
                    [&rt](Args... args) { return rt.static_call(args...); },
                    args));
            }
            else {
                discharge_deferred_comparison();
                spill_all_caller_save_regs();
                (rt.pass(std::move(std::get<Is>(elems))), ...);
                rt.call_impl();
            }
        }

        void status_code(monad::runtime::StatusCode);
        void error_block(asmjit::Label &, monad::runtime::StatusCode);
        void return_with_status_code(monad::runtime::StatusCode);

        template <typename... LiveSet>
        void
        jump_stack_elem_dest(StackElemRef &&, std::tuple<LiveSet...> const &);
        uint256_t literal_jump_dest_operand(StackElemRef &&);
        asmjit::Label const &jump_dest_label(uint256_t const &);
        void jump_literal_dest(uint256_t const &);
        template <typename... LiveSet>
        Operand non_literal_jump_dest_operand(
            StackElemRef const &, std::tuple<LiveSet...> const &);
        void jump_non_literal_dest(Operand const &, int32_t stack_adjustment);
        void conditional_jmp(asmjit::Label const &, Comparison);

        void read_context_address(int32_t offset);
        void read_context_word(int32_t offset);
        void read_context_uint32_to_word(int32_t offset);
        void read_context_uint64_to_word(int32_t offset);

        void lt(StackElemRef dst, StackElemRef src);
        void slt(StackElemRef dst, StackElemRef src);
        void
        cmp(StackElemRef dst, LocationType, StackElemRef src, LocationType);

        void byte_literal_ix(uint256_t const &ix, StackOffset src);
        void
        byte_general_reg_or_stack_offset_ix(StackElemRef ix, StackOffset src);

        template <typename... LiveSet>
        bool cmp_stack_elem_to_int32(
            StackElemRef, int32_t, asmjit::x86::Mem,
            std::tuple<LiveSet...> const &);

        void signextend_literal_ix(uint256_t const &ix, StackElemRef src);
        template <typename... LiveSet>
        void signextend_stack_elem_ix(
            StackElemRef ix, StackElemRef src, std::tuple<LiveSet...> const &);

        enum class ShiftType
        {
            SHL,
            SHR,
            SAR
        };

        template <ShiftType shift_type, typename... LiveSet>
        void shift_by_stack_elem(
            StackElemRef shift, StackElemRef, std::tuple<LiveSet...> const &);

        template <ShiftType shift_type, typename... LiveSet>
        void setup_shift_stack(StackElemRef, std::tuple<LiveSet...> const &);

        template <ShiftType shift_type, typename... LiveSet>
        void shift_by_literal(
            uint256_t shift, StackElemRef, std::tuple<LiveSet...> const &);

        template <ShiftType shift_type, typename... LiveSet>
        void shift_by_general_reg_or_stack_offset(
            StackElemRef shift, StackElemRef, std::tuple<LiveSet...> const &);

        template <bool commutative>
        std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>
        prepare_general_dest_and_source(
            StackElemRef dst, std::optional<int32_t> dst_stack_index,
            StackElemRef src);

        template <bool commutative, typename... LiveSet>
        std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>
        get_general_dest_and_source(
            StackElemRef dst, std::optional<int32_t> dst_stack_index,
            StackElemRef src, std::tuple<LiveSet...> const &);

        Operand get_operand(
            StackElemRef, LocationType, bool always_append_literal = false);

        template <
            GeneralBinInstr<asmjit::x86::Gp, asmjit::x86::Gp> GG,
            GeneralBinInstr<asmjit::x86::Gp, asmjit::x86::Mem> GM,
            GeneralBinInstr<asmjit::x86::Gp, asmjit::Imm> GI,
            GeneralBinInstr<asmjit::x86::Mem, asmjit::x86::Gp> MG,
            GeneralBinInstr<asmjit::x86::Mem, asmjit::Imm> MI>
        void general_bin_instr(Operand const &dst_op, Operand const &src_op);

        template <typename... LiveSet>
        std::tuple<StackElemRef, StackElemRef, LocationType> get_una_arguments(
            StackElemRef dst, std::optional<int32_t> dst_stack_index,
            std::tuple<LiveSet...> const &);

        template <typename... LiveSet>
        std::tuple<StackElemRef, LocationType, StackElemRef, LocationType>
        prepare_avx_or_general_arguments_commutative(
            StackElemRef dst, StackElemRef src, std::tuple<LiveSet...> const &);

        template <typename... LiveSet>
        std::tuple<
            StackElemRef, StackElemRef, LocationType, StackElemRef,
            LocationType>
        get_avx_or_general_arguments_commutative(
            StackElemRef dst, StackElemRef src, std::tuple<LiveSet...> const &);

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
        asmjit::Label bad_jumpdest_label_;
        asmjit::Label invalid_instruction_label_;
        asmjit::Label jump_table_label_;
        Stack stack_;
        std::array<Gpq256, 3> gpq256_regs_;
        GeneralReg rcx_general_reg;
        uint8_t rcx_general_reg_index;
        uint64_t bytecode_size_;
        std::unordered_map<byte_offset, asmjit::Label> jump_dests_;
        std::vector<std::pair<asmjit::Label, Literal>> literals_;
        std::vector<std::pair<asmjit::Label, void *>> external_functions_;
        std::vector<std::tuple<asmjit::Label, Gpq256, asmjit::Label>>
            byte_out_of_bounds_handlers_;
        std::vector<std::pair<asmjit::Label, std::string>> debug_messages_;
    };
}
