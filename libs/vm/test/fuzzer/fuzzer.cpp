#include "assertions.hpp"
#include "test_vm.hpp"

#include "account.hpp"
#include "host.hpp"
#include "state.hpp"

#include <monad/evm/opcodes.hpp>
#include <monad/fuzzing/generator/choice.hpp>
#include <monad/fuzzing/generator/generator.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/uint256.hpp>

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
#include <future>
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
using namespace monad::fuzzing;
using namespace std::chrono_literals;

using enum monad::compiler::EvmOpCode;

constexpr std::string_view to_string(evmc_status_code const sc) noexcept
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

constexpr auto genesis_address =
    0xBEEFCAFE000000000000000000000000BA5EBA11_address;

constexpr auto block_gas_limit = 300'000'000;

Account genesis_account() noexcept
{
    auto acct = Account{};
    acct.balance = std::numeric_limits<utils::uint256_t>::max();
    return acct;
}

State initial_state()
{
    auto init = State{};
    init.insert(genesis_address, genesis_account());
    return init;
}

Transaction tx_from(State &state, evmc::address const &addr) noexcept
{
    auto tx = Transaction{};
    tx.gas_limit = block_gas_limit;
    tx.sender = addr;
    tx.nonce = state.get_or_insert(addr).nonce;
    return tx;
}

constexpr auto deploy_prefix() noexcept
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

evmc::address deploy_contract(
    State &state, evmc::VM &vm, evmc::address const &from,
    std::span<std::uint8_t const> const code)
{
    auto const create_address =
        compute_create_address(from, state.get_or_insert(from).nonce);
    MONAD_COMPILER_DEBUG_ASSERT(state.find(create_address) == nullptr);

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
    MONAD_COMPILER_DEBUG_ASSERT(
        std::holds_alternative<TransactionReceipt>(res));

    MONAD_COMPILER_DEBUG_ASSERT(state.find(create_address) != nullptr);
    return create_address;
}

// Derived from the evmone transition implementation; transaction-related
// book-keeping is elided here to keep the implementation simple and allow us to
// send arbitrary messages to update the state.
evmc::Result transition(
    State &state, evmc_message const &msg, evmc_revision const rev,
    evmc::VM &vm, std::int64_t const block_gas_left)
{
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

    // Post-transaction clean-up.
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
void clean_storage(State &state)
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
    }
}

using random_engine_t = std::mt19937_64;

struct arguments
{
    using seed_t = random_engine_t::result_type;
    static constexpr seed_t default_seed = std::numeric_limits<seed_t>::max();

    std::size_t iterations_per_run = 100;
    std::size_t messages = 256;
    seed_t seed = default_seed;
    std::int64_t log_freq = 1000;
    std::size_t runs = std::numeric_limits<std::size_t>::max();
    bool print_stats = false;

    void set_random_seed_if_default()
    {
        if (seed == default_seed) {
            seed = std::random_device()();
        }
    }
};

arguments parse_args(int const argc, char **const argv)
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
        "Number of messages to send per iteration (default 256)");

    app.add_option(
        "--seed",
        args.seed,
        "Seed to use for reproducible fuzzing (random by default)");

    app.add_option(
        "--log-freq", args.log_freq, "Print logging every N iterations");

    app.add_option(
        "-r,--runs", args.runs, "Number of runs (evm state is reset between runs) (unbounded by default)");

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

evmc_status_code fuzz_iteration(
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
        clean_storage(evmone_state);
    }

    if (compiler_result.status_code != EVMC_SUCCESS) {
        compiler_state.rollback(compiler_checkpoint);
        clean_storage(compiler_state);
    }

    assert_equal(evmone_state, compiler_state);
    return evmone_result.status_code;
}

void log(
    std::chrono::high_resolution_clock::time_point &start,
    arguments const &args,
    std::unordered_map<evmc_status_code, std::size_t> const &exit_code_stats,
    std::int64_t const i, std::size_t const total_messages)
{
    using namespace std::chrono;

    if (i > 0 && i % args.log_freq == 0) {
        constexpr auto ns_factor = duration_cast<nanoseconds>(1s).count();

        auto const end = high_resolution_clock::now();
        auto const diff = (end - start).count();
        auto const per_contract = diff / args.log_freq;

        std::cerr << std::format(
            "[{}]: {:.4f}s / iteration\n",
            i,
            static_cast<double>(per_contract) / ns_factor);

        if (args.print_stats) {
            for (auto const &[k, v] : exit_code_stats) {
                auto const percentage = (static_cast<double>(v) /
                                         static_cast<double>(total_messages)) *
                                        100;
                std::cerr << std::format(
                    "  {:<21}: {:.2f}%\n", to_string(k), percentage);
            }
        }

        start = end;
    }
}

void do_run(arguments const &args)
{
    constexpr auto rev = EVMC_CANCUN;

    std::cerr << std::format("Fuzzing with seed: {}\n", args.seed);

    auto engine = random_engine_t(args.seed);

    auto evmone_vm = evmc::VM(evmc_create_evmone());
    auto compiler_vm = evmc::VM(new BlockchainTestVM());

    auto evmone_state = initial_state();
    auto compiler_state = initial_state();

    auto contract_addresses = std::vector<evmc::address>{};

    auto exit_code_stats = std::unordered_map<evmc_status_code, std::size_t>{};
    auto total_messages = std::size_t{0};

    auto last_start = std::chrono::high_resolution_clock::now();

    for (auto i = 0u; i < args.iterations_per_run; ++i) {
        auto const contract =
            monad::fuzzing::generate_program(engine, contract_addresses);

        auto const a =
            deploy_contract(evmone_state, evmone_vm, genesis_address, contract);
        auto const a1 = deploy_contract(
            compiler_state, compiler_vm, genesis_address, contract);

        MONAD_COMPILER_ASSERT(a == a1);
        assert_equal(evmone_state, compiler_state);

        contract_addresses.push_back(a);

        for (auto j = 0u; j < args.messages; ++j) {
            auto promise = std::promise<evmc_status_code>{};
            auto future = promise.get_future();

            auto const target =
                monad::fuzzing::uniform_sample(engine, contract_addresses);
            auto msg = monad::fuzzing::generate_message(
                engine,
                target,
                contract_addresses,
                {genesis_address},
                [&](auto const &address) {
                    if (auto *found = evmone_state.find(address);
                        found != nullptr) {
                        return found->code;
                    }

                    return evmc::bytes{};
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

        log(last_start, args, exit_code_stats, i, total_messages);
    }
}

int main(int argc, char **argv)
{
    auto args = parse_args(argc, argv);

    for (auto i = 0u; i < args.runs; ++i) {
        do_run(args);
        args.seed = random_engine_t(args.seed)();
    }

    return 0;
}
