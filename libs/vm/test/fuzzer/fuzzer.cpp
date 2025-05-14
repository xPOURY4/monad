#include "assertions.hpp"
#include "test_vm.hpp"

#include "account.hpp"
#include "host.hpp"
#include "state.hpp"

#include <monad/vm/compiler/ir/x86/emitter.hpp>
#include <monad/vm/compiler/ir/x86/virtual_stack.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/evm/opcodes.hpp>
#include <monad/vm/fuzzing/generator/choice.hpp>
#include <monad/vm/fuzzing/generator/generator.hpp>
#include <monad/vm/utils/debug.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <evmone/constants.hpp>
#include <evmone/evmone.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <intx/intx.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <bits/chrono.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace evmone::state;
using namespace evmc::literals;
using namespace monad;
using namespace monad::vm::fuzzing;
using namespace std::chrono_literals;

using enum monad::vm::compiler::EvmOpCode;

static constexpr std::string_view to_string(evmc_status_code const sc) noexcept
{
    switch (sc) {
    case EVMC_SUCCESS:
        return "SUCCESS";
    case EVMC_FAILURE:
        return "FAILURE";
    case EVMC_REVERT:
        return "REVERT";
    case EVMC_OUT_OF_GAS:
        return "OUT_OF_GAS";
    case EVMC_INVALID_INSTRUCTION:
        return "INVALID_INSTRUCTION";
    case EVMC_UNDEFINED_INSTRUCTION:
        return "UNDEFINED_INSTRUCTION";
    case EVMC_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    case EVMC_STACK_UNDERFLOW:
        return "STACK_UNDERFLOW";
    case EVMC_BAD_JUMP_DESTINATION:
        return "BAD_JUMP_DESTINATION";
    case EVMC_INVALID_MEMORY_ACCESS:
        return "INVALID_MEMORY_ACCESS";
    case EVMC_CALL_DEPTH_EXCEEDED:
        return "CALL_DEPTH_EXCEEDED";
    case EVMC_STATIC_MODE_VIOLATION:
        return "STATIC_MODE_VIOLATION";
    case EVMC_PRECOMPILE_FAILURE:
        return "PRECOMPILE_FAILURE";
    case EVMC_ARGUMENT_OUT_OF_RANGE:
        return "ARGUMENT_OUT_OF_RANGE";
    case EVMC_INSUFFICIENT_BALANCE:
        return "INSUFFICIENT_BALANCE";
    case EVMC_INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    case EVMC_REJECTED:
        return "REJECTED";
    case EVMC_OUT_OF_MEMORY:
        return "OUT_OF_MEMORY";
    default:
        return "OTHER";
    }
}

static constexpr auto genesis_address =
    0xBEEFCAFE000000000000000000000000BA5EBA11_address;

static constexpr auto block_gas_limit = 300'000'000;

static Account genesis_account() noexcept
{
    auto acct = Account{};
    acct.balance = std::numeric_limits<vm::utils::uint256_t>::max();
    return acct;
}

static State initial_state()
{
    auto init = State{};
    init.insert(genesis_address, genesis_account());
    return init;
}

static Transaction tx_from(State &state, evmc::address const &addr) noexcept
{
    auto tx = Transaction{};
    tx.gas_limit = block_gas_limit;
    tx.sender = addr;
    tx.nonce = state.get_or_insert(addr).nonce;
    return tx;
}

static constexpr auto deploy_prefix() noexcept
{
    return std::array<std::uint8_t, 11>{
        PUSH1,
        0x0B,
        DUP1,
        CODESIZE,
        SUB,
        DUP1,
        DUP3,
        PUSH0,
        CODECOPY,
        PUSH0,
        RETURN,
    };
}

static evmc::address deploy_contract(
    State &state, evmc::VM &vm, evmc::address const &from,
    std::span<std::uint8_t const> const code)
{
    auto const create_address =
        compute_create_address(from, state.get_or_insert(from).nonce);
    MONAD_VM_DEBUG_ASSERT(state.find(create_address) == nullptr);

    constexpr auto prefix = deploy_prefix();
    auto calldata = bytes{};

    calldata.reserve(prefix.size() + code.size());
    std::copy(prefix.begin(), prefix.end(), std::back_inserter(calldata));
    std::copy(code.begin(), code.end(), std::back_inserter(calldata));

    auto tx = tx_from(state, from);
    tx.data = calldata;

    auto block = BlockInfo{};

    auto res =
        transition(state, block, tx, EVMC_CANCUN, vm, block_gas_limit, 0);
    MONAD_VM_ASSERT(std::holds_alternative<TransactionReceipt>(res));
    MONAD_VM_ASSERT(state.find(create_address) != nullptr);

    return create_address;
}

// Derived from the evmone transition implementation; transaction-related
// book-keeping is elided here to keep the implementation simple and allow us to
// send arbitrary messages to update the state.
static evmc::Result transition(
    State &state, evmc_message const &msg, evmc_revision const rev,
    evmc::VM &vm, std::int64_t const block_gas_left)
{
    // Pre-transaction clean-up.
    // - Clear transient storage.
    // - Set accounts and their storage access status to cold.
    // - Clear the "just created" account flag.
    for (auto &[addr, acc] : state.get_accounts()) {
        acc.transient_storage.clear();
        acc.access_status = EVMC_ACCESS_COLD;
        acc.just_created = false;
        for (auto &[key, val] : acc.storage) {
            val.access_status = EVMC_ACCESS_COLD;
            val.original = val.current;
        }
    }

    // TODO(BSC): fill out block and host context properly; should all work fine
    // for the moment as zero values from the perspective of the VM
    // implementations.
    auto block = BlockInfo{};
    auto tx = tx_from(state, msg.sender);
    tx.to = msg.recipient;

    constexpr auto effective_gas_price = 10;

    auto *sender_ptr = state.find(msg.sender);
    auto &sender_acc =
        (sender_ptr != nullptr) ? *sender_ptr : state.insert(msg.sender);

    ++sender_acc.nonce;
    sender_acc.balance -= block_gas_left * effective_gas_price;

    Host host{rev, vm, state, block, tx};

    sender_acc.access_status = EVMC_ACCESS_WARM; // Tx sender is always warm.
    if (tx.to.has_value()) {
        host.access_account(*tx.to);
    }

    auto result = host.call(msg);
    auto gas_used = block_gas_left - result.gas_left;

    auto const max_refund_quotient = rev >= EVMC_LONDON ? 5 : 2;
    auto const refund_limit = gas_used / max_refund_quotient;
    auto const refund = std::min(result.gas_refund, refund_limit);
    gas_used -= refund;

    sender_acc.balance += (block_gas_left - gas_used) * effective_gas_price;

    // Apply destructs.
    std::erase_if(
        state.get_accounts(),
        [](std::pair<address const, Account> const &p) noexcept {
            return p.second.destructed;
        });

    // Delete empty accounts after every transaction. This is strictly required
    // until Byzantium where intermediate state root hashes are part of the
    // transaction receipt.
    // TODO: Consider limiting this only to Spurious Dragon.
    if (rev >= EVMC_SPURIOUS_DRAGON) {
        std::erase_if(
            state.get_accounts(),
            [](std::pair<address const, Account> const &p) noexcept {
                auto const &acc = p.second;
                return acc.erase_if_empty && acc.is_empty();
            });
    }

    return result;
}

// It's possible for the compiler and evmone to reach equivalent-but-not-equal
// states after both executing. For example, the compiler may exit a block
// containing an SSTORE early because of unconditional underflow later in the
// block. Evmone will instead execute the SSTORE, then roll back the change.
// Because of how rollback is implemented, this produces a state with a mapping
// `K |-> 0` for some key `K`. This won't directly compare equal to the _empty_
// state that the compiler has, and so we need to normalise the states after
// execution to remove cold zero slots.
static void clean_storage(State &state)
{
    for (auto &[addr, acc] : state.get_accounts()) {
        for (auto it = acc.storage.begin(); it != acc.storage.end();) {
            auto const &[k, v] = *it;

            if (v.current == evmc::bytes32{} && v.original == evmc::bytes32{} &&
                v.access_status == EVMC_ACCESS_COLD) {
                it = acc.storage.erase(it);
            }
            else {
                ++it;
            }
        }
        for (auto it = acc.transient_storage.begin();
             it != acc.transient_storage.end();) {
            auto const &[k, v] = *it;
            if (v == evmc::bytes32{}) {
                it = acc.transient_storage.erase(it);
            }
            else {
                ++it;
            }
        }
    }
}

using random_engine_t = std::mt19937_64;

namespace
{
    struct arguments
    {
        using seed_t = random_engine_t::result_type;
        static constexpr seed_t default_seed =
            std::numeric_limits<seed_t>::max();

        std::int64_t iterations_per_run = 100;
        std::size_t messages = 4;
        seed_t seed = default_seed;
        std::size_t runs = std::numeric_limits<std::size_t>::max();
        bool print_stats = false;

        void set_random_seed_if_default()
        {
            if (seed == default_seed) {
                seed = std::random_device()();
            }
        }
    };
}

static arguments parse_args(int const argc, char **const argv)
{
    auto app = CLI::App("Fuzz-test Monad EVM compiler");
    auto args = arguments{};

    app.add_option(
        "-i,--iterations-per-run",
        args.iterations_per_run,
        "Number of fuzz iterations in each run (default 100)");

    app.add_option(
        "-n,--messages",
        args.messages,
        "Number of messages to send per iteration (default 4)");

    app.add_option(
        "--seed",
        args.seed,
        "Seed to use for reproducible fuzzing (random by default)");

    app.add_option(
        "-r,--runs",
        args.runs,
        "Number of runs (evm state is reset between runs) (unbounded by "
        "default)");

    app.add_flag(
        "--print-stats",
        args.print_stats,
        "Print message result statistics when logging");

    try {
        app.parse(argc, argv);
    }
    catch (CLI::ParseError const &e) {
        std::exit(app.exit(e));
    }

    args.set_random_seed_if_default();
    return args;
}

static evmc_status_code fuzz_iteration(
    evmc_message const &msg, evmc_revision const rev, State &evmone_state,
    evmc::VM &evmone_vm, State &compiler_state, evmc::VM &compiler_vm)
{
    for (State &state : {std::ref(evmone_state), std::ref(compiler_state)}) {
        state.get_or_insert(msg.sender);
        state.get_or_insert(msg.recipient);
    }

    auto const evmone_checkpoint = evmone_state.checkpoint();
    auto const evmone_result =
        transition(evmone_state, msg, rev, evmone_vm, block_gas_limit);

    auto const compiler_checkpoint = compiler_state.checkpoint();
    auto const compiler_result =
        transition(compiler_state, msg, rev, compiler_vm, block_gas_limit);

    assert_equal(evmone_result, compiler_result);

    if (evmone_result.status_code != EVMC_SUCCESS) {
        evmone_state.rollback(evmone_checkpoint);
    }
    clean_storage(evmone_state);

    if (compiler_result.status_code != EVMC_SUCCESS) {
        compiler_state.rollback(compiler_checkpoint);
    }
    clean_storage(compiler_state);

    assert_equal(evmone_state, compiler_state);
    return evmone_result.status_code;
}

static void
log(std::chrono::high_resolution_clock::time_point start, arguments const &args,
    std::unordered_map<evmc_status_code, std::size_t> const &exit_code_stats,
    std::size_t const run_index, std::size_t const total_messages)
{
    using namespace std::chrono;

    constexpr auto ns_factor = duration_cast<nanoseconds>(1s).count();

    auto const end = high_resolution_clock::now();
    auto const diff = (end - start).count();
    auto const per_contract = diff / args.iterations_per_run;

    std::cerr << std::format(
        "[{}]: {:.4f}s / iteration\n",
        run_index + 1,
        static_cast<double>(per_contract) / ns_factor);

    if (args.print_stats) {
        for (auto const &[k, v] : exit_code_stats) {
            auto const percentage =
                (static_cast<double>(v) / static_cast<double>(total_messages)) *
                100;
            std::cerr << std::format(
                "  {:<21}: {:.2f}%\n", to_string(k), percentage);
        }
    }
}

static void do_run(std::size_t const run_index, arguments const &args)
{
    constexpr auto rev = EVMC_CANCUN;

    std::cerr << std::format("Fuzzing with seed: {}\n", args.seed);

    auto engine = random_engine_t(args.seed);

    static constexpr std::array<double, 2> artificial_swap_probs = {0, 0.50};
    double const artificial_swap_prob =
        uniform_sample(engine, artificial_swap_probs);

    static constexpr std::array<double, 2> artificial_peak_probs = {0, 0.75};
    double const artificial_peak_prob =
        uniform_sample(engine, artificial_peak_probs);

    static constexpr std::array<double, 3> artificial_avx_probs = {0, 0.5, 1.0};
    double const artificial_avx_prob =
        uniform_sample(engine, artificial_avx_probs);

    static constexpr std::array<double, 3> artificial_general_probs = {
        0, 0.5, 1.0};
    double const artificial_general_prob =
        uniform_sample(engine, artificial_general_probs);

    double const artificial_top2_prob =
        std::min(1.0, artificial_avx_prob + artificial_general_prob);

    auto post_instruction_emit_hook = [&](vm::compiler::native::Emitter &emit) {
        // The fuzzer has a hard time exploring edge case virtual stack
        // states. To circumvent this we will artificially change the state
        // of the stack to increase probability of having stack elements in
        // different locations.

        using monad::vm::compiler::native::GENERAL_REG_COUNT;
        using monad::vm::compiler::native::GeneralReg;

        auto &stack = emit.get_stack();
        if (stack.top_index() < stack.min_delta()) {
            // Do nothing when the stack is empty.
            return;
        }

        emit.checked_debug_comment("BEGIN artificial setup");

        // Potentially move around the rdx and/or rcx register.
        with_probability(engine, artificial_swap_prob, [&](auto &) {
            emit.swap_rdx_general_reg_if_free();
        });
        with_probability(engine, artificial_swap_prob, [&](auto &) {
            emit.swap_rdx_general_reg_index_if_free();
        });
        with_probability(engine, artificial_swap_prob, [&](auto &) {
            emit.swap_rcx_general_reg_if_free();
        });
        with_probability(engine, artificial_swap_prob, [&](auto &) {
            emit.swap_rcx_general_reg_index_if_free();
        });

        auto mov_to_stack_offset = [&](std::int32_t i) -> bool {
            if (stack.has_deferred_comparison_at(i)) {
                return false;
            }
            if (stack.get(i)->stack_offset()) {
                return true;
            }
            emit.mov_stack_index_to_stack_offset(i);
            return true;
        };

        auto mov_to_avx_reg = [&](std::int32_t i) -> bool {
            if (stack.has_deferred_comparison_at(i)) {
                return false;
            }
            if (stack.get(i)->avx_reg()) {
                return true;
            }
            emit.mov_stack_index_to_avx_reg(i);
            return true;
        };

        auto mov_to_general_reg = [&](std::int32_t i) -> bool {
            if (stack.has_deferred_comparison_at(i)) {
                return false;
            }
            if (stack.get(i)->general_reg()) {
                return true;
            }
            emit.mov_stack_index_to_general_reg(i);
            return true;
        };

        auto mov_to_locations = [&](std::int32_t i,
                                    bool lit,
                                    bool gen,
                                    bool avx,
                                    bool sta) -> bool {
            if (stack.has_deferred_comparison_at(i)) {
                return false;
            }

            auto e = stack.get(i);

            // Make sure at least one location is present:
            if (e->literal() && (lit | gen | avx | sta) == false) {
                auto dist = std::uniform_int_distribution<int>{0, 3};
                switch (dist(engine)) {
                case 0:
                    lit = true;
                    break;
                case 1:
                    gen = true;
                    break;
                case 2:
                    avx = true;
                    break;
                case 3:
                    sta = true;
                    break;
                }
            }
            else if ((gen | avx | sta) == false) {
                auto dist = std::uniform_int_distribution<int>{1, 3};
                switch (dist(engine)) {
                case 1:
                    gen = true;
                    break;
                case 2:
                    avx = true;
                    break;
                case 3:
                    sta = true;
                    break;
                }
            }

            if (gen) {
                mov_to_general_reg(i);
            }
            if (avx) {
                mov_to_avx_reg(i);
            }
            if (sta) {
                mov_to_stack_offset(i);
            }
            if (!lit && e->literal()) {
                stack.spill_literal(e);
            }
            if (!gen && e->general_reg()) {
                auto *s = stack.spill_general_reg(e);
                MONAD_VM_ASSERT(s == nullptr);
            }
            if (!avx && e->avx_reg()) {
                auto *s = stack.spill_avx_reg(e);
                MONAD_VM_ASSERT(s == nullptr);
            }
            if (!sta && e->stack_offset()) {
                stack.spill_stack_offset(e);
            }
            return true;
        };

        // At stack peak, we will spill everything to memory with
        // some probability to test that we do not run out of stack
        // offsets during the next instruction.
        if (stack.top_index() == stack.max_delta() - 1) {
            with_probability(engine, artificial_peak_prob, [&](auto &) {
                auto e = stack.max_delta();
                for (auto i = stack.min_delta(); i < e; ++i) {
                    mov_to_stack_offset(i);
                }
            });
        }

        with_probability(engine, artificial_avx_prob, [&](auto &) {
            // Try to write 13 to 16 stack elems to avx reg location.
            auto ndist = std::uniform_int_distribution<std::int32_t>{13, 16};
            auto const n = ndist(engine);
            auto offdist = std::uniform_int_distribution<std::int32_t>{2, 5};
            auto off = offdist(engine);
            auto const d = stack.min_delta();
            std::int32_t count = 0;
            for (auto i = stack.top_index() - off; i >= d; --i) {
                count += mov_to_avx_reg(i);
                if (count == n) {
                    break;
                }
            }
        });

        with_probability(engine, artificial_general_prob, [&](auto &) {
            // Try to write -3 to 3 stack elems to general reg location,
            // negative meaning spill (remove general reg locations).
            auto ndist = std::uniform_int_distribution<std::int32_t>{-3, 3};
            auto const n = ndist(engine);

            if (n == 0) {
                return;
            }

            if (n > 0) {
                auto offdist =
                    std::uniform_int_distribution<std::int32_t>{2, 5};
                auto off = offdist(engine);
                auto const d = stack.min_delta();
                std::int32_t count = 0;
                for (auto i = stack.top_index() - off; i >= d; --i) {
                    count += mov_to_general_reg(i);
                    if (count == n) {
                        break;
                    }
                }
                return;
            }

            auto gdist = std::uniform_int_distribution<std::uint8_t>{
                0, GENERAL_REG_COUNT - 1};
            std::uint8_t g = gdist(engine);
            size_t count = 0;
            for (auto i = n; i < 0 && count < GENERAL_REG_COUNT; ++count) {
                auto *e = stack.general_reg_stack_elem(GeneralReg{g});
                g = uint8_t((g + 1) % GENERAL_REG_COUNT);

                if (!e) {
                    continue;
                }

                auto const &ixs = e->stack_indices();
                MONAD_VM_ASSERT(!ixs.empty());
                auto ix = *ixs.begin();
                if (!e->literal() && !e->stack_offset() && !e->avx_reg()) {
                    emit.mov_stack_index_to_stack_offset(ix);
                }
                auto *s = stack.spill_general_reg(e);
                MONAD_VM_ASSERT(s == nullptr);
            }
        });

        with_probability(engine, artificial_top2_prob, [&](auto &) {
            // Try to put the top 2 stack elements in specific locations.

            auto i = std::max(stack.top_index() - 1, stack.min_delta());
            auto e = stack.top_index() + 1;
            auto dist = std::uniform_int_distribution<int>{0, 1};
            for (; i < e; ++i) {
                bool const lit = dist(engine) == 1;
                bool const gen = dist(engine) == 1;
                bool const avx = dist(engine) == 1;
                bool const sta = dist(engine) == 1;
                mov_to_locations(i, lit, gen, avx, sta);
            }

            // Swap general registers to increase variance of
            // general register locations.
            auto *x0 = stack.general_reg_stack_elem(GeneralReg{0});
            auto *x1 = stack.general_reg_stack_elem(GeneralReg{1});
            auto *x2 = stack.general_reg_stack_elem(GeneralReg{2});
            if (x0 && x1 && x2) {
                if (dist(engine) == 0) {
                    emit.swap_general_regs(*x0, *x1);
                }
                else {
                    emit.swap_general_regs(*x1, *x2);
                }
            }
            else if (x0 && x1) {
                emit.swap_general_regs(*x0, *x1);
            }
            else if (x1 && x2) {
                emit.swap_general_regs(*x1, *x2);
            }
            else if (x0 && x2) {
                emit.swap_general_regs(*x0, *x2);
            }
        });

        emit.checked_debug_comment("END artificial setup");
    };

    auto evmone_vm = evmc::VM(evmc_create_evmone());
    auto compiler_vm = evmc::VM(new BlockchainTestVM(
        BlockchainTestVM::Implementation::Compiler,
        post_instruction_emit_hook));

    auto evmone_state = initial_state();
    auto compiler_state = initial_state();

    auto contract_addresses = std::vector<evmc::address>{};

    auto exit_code_stats = std::unordered_map<evmc_status_code, std::size_t>{};
    auto total_messages = std::size_t{0};

    auto start_time = std::chrono::high_resolution_clock::now();

    for (auto i = 0; i < args.iterations_per_run; ++i) {
        using monad::vm::fuzzing::GeneratorFocus;
        auto focus = discrete_choice<GeneratorFocus>(
            engine,
            [](auto &) { return GeneratorFocus::Generic; },
            Choice(0.60, [](auto &) { return GeneratorFocus::Pow2; }),
            Choice(0.05, [](auto &) { return GeneratorFocus::DynJump; }));

        for (;;) {
            auto const contract = monad::vm::fuzzing::generate_program(
                focus, engine, contract_addresses);
            if (contract.size() > evmone::MAX_CODE_SIZE) {
                // The evmone host will fail when we attempt to deploy
                // contracts of this size. It rarely happens that we
                // generate contract this large.
                std::cerr << "Skipping contract of size: " << contract.size()
                          << " bytes" << std::endl;
                continue;
            }

            auto const a = deploy_contract(
                evmone_state, evmone_vm, genesis_address, contract);
            auto const a1 = deploy_contract(
                compiler_state, compiler_vm, genesis_address, contract);
            MONAD_VM_ASSERT(a == a1);

            assert_equal(evmone_state, compiler_state);

            contract_addresses.push_back(a);
            break;
        }

        for (auto j = 0u; j < args.messages; ++j) {
            auto const target =
                monad::vm::fuzzing::uniform_sample(engine, contract_addresses);
            auto msg = monad::vm::fuzzing::generate_message(
                focus,
                engine,
                target,
                contract_addresses,
                {genesis_address},
                [&](auto const &address) {
                    if (auto *found = evmone_state.find(address);
                        found != nullptr) {
                        return found->code;
                    }

                    return std::pair{evmc::bytes{}, empty_code_hash};
                });
            ++total_messages;

            auto const ec = fuzz_iteration(
                *msg,
                rev,
                evmone_state,
                evmone_vm,
                compiler_state,
                compiler_vm);
            ++exit_code_stats[ec];
        }
    }

    log(start_time, args, exit_code_stats, run_index, total_messages);
}

static void run_loop(int argc, char **argv)
{
    auto args = parse_args(argc, argv);
    for (auto i = 0u; i < args.runs; ++i) {
        do_run(i, args);
        args.seed = random_engine_t(args.seed)();
    }
}

int main(int argc, char **argv)
{
    if (monad::vm::utils::is_fuzzing_monad_vm) {
        run_loop(argc, argv);
        return 0;
    }
    std::cerr << "\nFuzzer not started:\n"
                 "Make sure to configure with -DMONAD_COMPILER_TESTING=ON and\n"
                 "set environment variable MONAD_COMPILER_FUZZING=1 before\n"
                 "starting the fuzzer\n";
    return 1;
}
