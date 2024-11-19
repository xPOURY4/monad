#pragma once

#include "compiler/ir/local_stacks.h"
#include <compiler/ir/x86.h>
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

        Emitter(
            asmjit::JitRuntime const &, char const *debug_log_file = nullptr);
        ~Emitter();
        bool block_prologue(Block const &);
        void block_epilogue(Block const &);
        void gas_decrement_no_check(int64_t);
        void gas_decrement_check_non_negative(int64_t);
        void stop();
        void return_();
        void revert();

        entrypoint_t finish_contract(asmjit::JitRuntime &);

    private:
        asmjit::CodeHolder *
        init_code_holder(asmjit::JitRuntime const &, char const *);
        void contract_prologue();
        void contract_epilogue();
        void status_code(monad::runtime::StatusCode);
        void error_block(asmjit::Label &, monad::runtime::StatusCode);

        // Order of fields is significant.
        EmitErrorHandler error_handler_;
        asmjit::CodeHolder code_holder_;
        asmjit::FileLogger debug_logger_;
        asmjit::x86::Assembler as_;
        asmjit::Label epilogue_label_;
        asmjit::Label out_of_gas_label_;
        asmjit::Label overflow_label_;
        asmjit::Label underflow_label_;
    };
}
