#include "asmjit/core/jitruntime.h"
#include "evmc/evmc.hpp"
#include "runtime/types.h"
#include <compiler/ir/x86.h>
#include <compiler/ir/x86/emitter.h>

#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace runtime = monad::runtime;
using namespace monad::compiler;
using namespace monad::compiler::native;

namespace
{
    evmc::address max_address()
    {
        evmc::address ret;
        std::memset(ret.bytes, -1, sizeof(ret.bytes) / sizeof(*ret.bytes));
        return ret;
    }

    evmc::bytes32 max_bytes32()
    {
        evmc::bytes32 ret;
        std::memset(ret.bytes, -1, sizeof(ret.bytes) / sizeof(*ret.bytes));
        return ret;
    }

    runtime::Context test_context(int64_t gas_remaining = 10)
    {
        return runtime::Context{
            .host = nullptr,
            .context = nullptr,
            .gas_remaining = gas_remaining,
            .gas_refund = 0,
            .env =
                {
                    .evmc_flags = 0,
                    .depth = 0,
                    .recipient = max_address(),
                    .sender = max_address(),
                    .value = max_bytes32(),
                    .create2_salt = max_bytes32(),
                    .return_data = {},
                },
            .memory = {},
            .memory_cost = 0,
        };
    }

    runtime::Result test_result()
    {
        return runtime::Result{
            .offset = max_bytes32(),
            .size = max_bytes32(),
            .status = static_cast<runtime::StatusCode>(
                std::numeric_limits<uint64_t>::max()),

        };
    }
}

TEST(Emitter, empty)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(
        static_cast<uint64_t>(ret.status),
        std::numeric_limits<uint64_t>::max());
}

TEST(Emitter, stop)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, gas_decrement_no_check_1)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_no_check(2);

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 3);
}

TEST(Emitter, gas_decrement_no_check_2)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_no_check(7);

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, -2);
}

TEST(Emitter, gas_decrement_check_non_negative_1)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_check_non_negative(6);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, -1);
    ASSERT_EQ(ret.status, runtime::StatusCode::OutOfGas);
}

TEST(Emitter, gas_decrement_check_non_negative_2)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_check_non_negative(5);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 0);
    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, gas_decrement_check_non_negative_3)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_check_non_negative(4);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 1);
    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}
