#include "test_vm.hpp"

#include "account.hpp"
#include "host.hpp"
#include "state.hpp"

#include <monad/compiler/evm_opcodes.hpp>
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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <span>
#include <utility>
#include <variant>
#include <vector>

using namespace evmone::state;
using namespace evmc::literals;
using namespace monad;

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

void assert_equal(StorageValue const &a, StorageValue const &b)
{
    MONAD_COMPILER_ASSERT(a.current == b.current);
    MONAD_COMPILER_ASSERT(a.original == b.original);
    MONAD_COMPILER_ASSERT(a.access_status == b.access_status);
}

void assert_equal(Account const &a, Account const &b)
{
    MONAD_COMPILER_ASSERT(
        a.transient_storage.size() == b.transient_storage.size());
    for (auto const &[k, v] : a.transient_storage) {
        auto const found = b.transient_storage.find(k);
        MONAD_COMPILER_ASSERT(found != b.transient_storage.end());
        MONAD_COMPILER_ASSERT(found->second == v);
    }

    MONAD_COMPILER_ASSERT(a.storage.size() == b.storage.size());
    for (auto const &[k, v] : a.storage) {
        auto const found = b.storage.find(k);
        MONAD_COMPILER_ASSERT(found != b.storage.end());
        assert_equal(v, found->second);
    }

    MONAD_COMPILER_ASSERT(a.nonce == b.nonce);
    MONAD_COMPILER_ASSERT(a.balance == b.balance);
    MONAD_COMPILER_ASSERT(a.code == b.code);
    MONAD_COMPILER_ASSERT(a.destructed == b.destructed);
    MONAD_COMPILER_ASSERT(a.erase_if_empty == b.erase_if_empty);
    MONAD_COMPILER_ASSERT(a.just_created == b.just_created);
    MONAD_COMPILER_ASSERT(a.access_status == b.access_status);
}

void assert_equal(State const &a, State const &b)
{
    auto const &a_accs = a.get_accounts();
    auto const &b_accs = b.get_accounts();

    MONAD_COMPILER_ASSERT(a_accs.size() == b_accs.size());
    for (auto const &[k, v] : a_accs) {
        auto const found = b_accs.find(k);
        MONAD_COMPILER_ASSERT(found != b_accs.end());
        assert_equal(v, found->second);
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
    assert(gas_used > 0);

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

// TODO(BSC: fill in actual message generation
template <typename Engine>
evmc_message generate_message(Engine &eng, evmc::address const &target) noexcept
{
    (void)eng;
    return evmc_message{
        .kind = EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = block_gas_limit,
        .recipient = target,
        .sender = genesis_address,
        .input_data = nullptr,
        .input_size = 0,
        .value = {},
        .create2_salt = {},
        .code_address = target,
        .code = nullptr,
        .code_size = 0,
    };
}

// TODO(BSC: fill in actual contract generation
template <typename Engine>
std::vector<std::uint8_t> generate_contract(Engine &eng)
{
    auto dist = std::uniform_int_distribution<std::uint8_t>();

    // Placeholder to demonstrate stats printing; will exit successfully 50% of
    // the time and OOG the remaining 50%.
    return {
        PUSH1,    0x2,   PUSH1,     dist(eng), MOD,       PUSH1,  0x0E,
        JUMPI,    PUSH1, dist(eng), PUSH1,     dist(eng), SSTORE, STOP,
        JUMPDEST, PUSH1, 0x01,      PUSH0,     SUB,       DUP1,   MSTORE,
    };
}

using random_engine_t = std::mt19937_64;

struct arguments
{
    using seed_t = random_engine_t::result_type;
    static constexpr seed_t default_seed = std::numeric_limits<seed_t>::max();

    std::size_t iterations = std::numeric_limits<std::size_t>::max();
    std::size_t messages = 256;
    seed_t seed = default_seed;
    std::int64_t log_freq = 1000;
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
        "-i,--iterations",
        args.iterations,
        "Number of fuzz iterations to run (unbounded by default)");

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

    return args;
}

int main(int argc, char **argv)
{
    constexpr auto rev = EVMC_CANCUN;

    auto args = parse_args(argc, argv);
    args.set_random_seed_if_default();

    std::cerr << std::format("Fuzzing with seed: {}\n", args.seed);

    auto engine = random_engine_t(args.seed);

    auto evmone_vm = evmc::VM(evmc_create_evmone());
    auto compiler_vm = evmc::VM(new BlockchainTestVM());

    auto evmone_state = initial_state();
    auto compiler_state = initial_state();

    auto exit_code_stats = std::unordered_map<evmc_status_code, std::size_t>{};
    auto total_messages = std::size_t{0};

    auto last_start = std::chrono::high_resolution_clock::now();

    for (auto i = 0u; i < args.iterations; ++i) {
        auto const contract = generate_contract(engine);

        auto const a =
            deploy_contract(evmone_state, evmone_vm, genesis_address, contract);
        auto const a1 = deploy_contract(
            compiler_state, compiler_vm, genesis_address, contract);

        MONAD_COMPILER_ASSERT(a == a1);
        assert_equal(evmone_state, compiler_state);

        for (auto j = 0u; j < args.messages; ++j) {
            auto const msg = generate_message(engine, a);
            ++total_messages;

            auto const evmone_checkpoint = evmone_state.checkpoint();
            auto const evmone_result =
                transition(evmone_state, msg, rev, evmone_vm, block_gas_limit);

            ++exit_code_stats[evmone_result.status_code];

            if (evmone_result.status_code != EVMC_SUCCESS) {
                evmone_state.rollback(evmone_checkpoint);
            }

            auto const compiler_checkpoint = compiler_state.checkpoint();
            auto const compiler_result = transition(
                compiler_state, msg, rev, compiler_vm, block_gas_limit);

            if (compiler_result.status_code != EVMC_SUCCESS) {
                compiler_state.rollback(compiler_checkpoint);
            }

            assert_equal(evmone_state, compiler_state);
        }

        if (i > 0 && i % args.log_freq == 0) {
            using namespace std::chrono;
            using namespace std::chrono_literals;

            constexpr auto ns_factor = duration_cast<nanoseconds>(1s).count();

            auto const end = high_resolution_clock::now();
            auto const diff = (end - last_start).count();
            auto const per_contract = diff / args.log_freq;

            std::cerr << std::format(
                "[{}]: {:.4f}s / iteration\n",
                i,
                static_cast<double>(per_contract) / ns_factor);

            if (args.print_stats) {
                for (auto const &[k, v] : exit_code_stats) {
                    auto const percentage =
                        (static_cast<double>(v) /
                         static_cast<double>(total_messages)) *
                        100;
                    std::cerr << std::format(
                        "  {:<21}: {:.2f}%\n", to_string(k), percentage);
                }
            }

            last_start = end;
        }
    }
}
