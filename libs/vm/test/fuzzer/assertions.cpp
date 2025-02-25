#include "account.hpp"
#include "state.hpp"

#include <monad/utils/assert.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <algorithm>
#include <span>

using namespace evmone::state;

namespace monad::fuzzing
{
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

    void assert_equal(
        evmc::Result const &evmone_result, evmc::Result const &compiler_result)
    {
        MONAD_COMPILER_ASSERT(std::ranges::equal(
            evmone_result.create_address.bytes,
            compiler_result.create_address.bytes));

        MONAD_COMPILER_ASSERT(
            evmone_result.gas_left == compiler_result.gas_left);
        MONAD_COMPILER_ASSERT(
            evmone_result.gas_refund == compiler_result.gas_refund);

        MONAD_COMPILER_ASSERT(std::ranges::equal(
            std::span(evmone_result.output_data, evmone_result.output_size),
            std::span(
                compiler_result.output_data, compiler_result.output_size)));

        switch (evmone_result.status_code) {
        case EVMC_SUCCESS:
        case EVMC_REVERT:
            MONAD_COMPILER_ASSERT(
                evmone_result.status_code == compiler_result.status_code);
            break;
        default:
            MONAD_COMPILER_ASSERT(compiler_result.status_code != EVMC_SUCCESS);
            MONAD_COMPILER_ASSERT(compiler_result.status_code != EVMC_REVERT);
            break;
        }
    }

}
