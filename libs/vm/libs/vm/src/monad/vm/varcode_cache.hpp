#pragma once

#include <monad/vm/code.hpp>
#include <monad/vm/utils/evmc_utils.hpp>
#include <monad/vm/utils/lru_weight_cache.hpp>

namespace monad::vm
{
    class VarcodeCache
    {
        static constexpr std::uint32_t default_max_cache_kb =
            std::uint64_t{1} << 22; // 4MB * 1kB = 4GB

        using WeightCache = utils::LruWeightCache<
            evmc::bytes32, SharedVarcode, utils::Hash32Compare>;

    public:
        VarcodeCache(std::uint32_t max_cache_kb = default_max_cache_kb);

        /// Get varcode for given code hash.
        std::optional<SharedVarcode> get(evmc::bytes32 const &code_hash);

        /// Insert into cache under `code_hash`.
        void
        set(evmc::bytes32 const &code_hash, SharedIntercode const &,
            SharedNativecode const &);

        /// Find varcode under `code_hash`, otherwise insert into cache.
        SharedVarcode
        try_set(evmc::bytes32 const &code_hash, SharedIntercode const &);

    private:
        WeightCache weight_cache_;
    };
}
