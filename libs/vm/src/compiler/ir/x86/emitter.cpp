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
#include "runtime/types.h"
#include "utils/assert.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <string>

namespace runtime = monad::runtime;
namespace x86 = asmjit::x86;

static_assert(ASMJIT_ARCH_X86 == 64);

constexpr auto reg_scratch = x86::rax;
constexpr auto reg_context = x86::rbx;
constexpr auto reg_stack = x86::rbp;

constexpr auto context_offset_gas_remaining =
    offsetof(runtime::Context, gas_remaining);

// constexpr auto result_offset_offset = offsetof(runtime::Result, offset);
// constexpr auto result_offset_size = offsetof(runtime::Result, size);
constexpr auto result_offset_status = offsetof(runtime::Result, status);

constexpr auto sp_offset_arg1 = 0;
constexpr auto sp_offset_arg2 = sp_offset_arg1 + 8;
constexpr auto sp_offset_arg3 = sp_offset_arg2 + 8;
constexpr auto sp_offset_arg4 = sp_offset_arg3 + 8;
constexpr auto sp_offset_arg5 = sp_offset_arg4 + 8;
constexpr auto sp_offset_stack_size = sp_offset_arg5 + 8;
constexpr auto sp_offset_result_ptr = sp_offset_stack_size + 8;

constexpr auto stack_frame_size = sp_offset_result_ptr + 8;

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

    Emitter::Emitter(asmjit::JitRuntime const &rt, char const *log_path)
        : as_{init_code_holder(rt, log_path)}
        , epilogue_label_{as_.newLabel()}
        , out_of_gas_label_{as_.newLabel()}
        , overflow_label_{as_.newLabel()}
        , underflow_label_{as_.newLabel()}
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
        error_block(out_of_gas_label_, runtime::StatusCode::OutOfGas);
        error_block(overflow_label_, runtime::StatusCode::Overflow);
        error_block(underflow_label_, runtime::StatusCode::Underflow);

        entrypoint_t contract_main;
        auto err = rt.add(&contract_main, &code_holder_);
        if (err != asmjit::kErrorOk) {
            throw Error{asmjit::DebugUtils::errorAsString(err)};
        }
        return contract_main;
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

    void Emitter::status_code(runtime::StatusCode status)
    {
        as_.mov(reg_scratch, x86::ptr(x86::rsp, sp_offset_result_ptr));
        uint64_t const c = static_cast<uint64_t>(status);
        as_.mov(x86::qword_ptr(reg_scratch, result_offset_status), c);
    }

    void Emitter::error_block(asmjit::Label &lbl, runtime::StatusCode status)
    {
        as_.bind(lbl);
        status_code(status);
        as_.jmp(epilogue_label_);
    }

    bool Emitter::block_prologue(Block const &)
    {
        // TODO
        return false;
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

    void Emitter::stop()
    {
        status_code(runtime::StatusCode::Success);
        as_.jmp(epilogue_label_);
    }

    void Emitter::return_()
    {
        status_code(runtime::StatusCode::Success);
        // TODO move offset and size to result ptr in reg_scratch.
        as_.jmp(epilogue_label_);
    }

    void Emitter::revert()
    {
        status_code(runtime::StatusCode::Revert);
        // TODO move offset and size to result ptr in reg_scratch.
        as_.jmp(epilogue_label_);
    }
}
