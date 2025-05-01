#pragma once

#include <monad/vm/code.hpp>
#include <monad/vm/utils/hash32_compare.hpp>
#include <monad/vm/utils/lru_weight_cache.hpp>

namespace monad::vm
{
    class VarcodeCache
    {
        static constexpr std::uint32_t default_max_cache_kb =
            std::uint64_t{1} << 22; // 4MB * 1kB = 4GB

    public:
        VarcodeCache(std::uint32_t max_cache_kb = default_max_cache_kb);

        /// Get varcode for given revision and code hash.
        std::optional<Varcode>
        get(evmc_revision, evmc::bytes32 const &code_hash);

        /// Set varcode for given revision and code hash.
        void
        set(evmc_revision, evmc::bytes32 const &code_hash, Varcode const &);

    private:
        struct RevisionedVarcode
        {
            evmc_revision revision;
            Varcode varcode;

            std::uint32_t cache_weight() const
            {
                std::size_t x = varcode.code_size_estimate();
                MONAD_VM_DEBUG_ASSERT(
                    x <= std::numeric_limits<uint32_t>::max());
                // Byte size in kB, plus 3 kB overhead:
                return (static_cast<std::uint32_t>(x) >> 10) + 3;
            }
        };

        using WeightCache = utils::LruWeightCache<
            evmc::bytes32, RevisionedVarcode, Hash32Compare>;

        WeightCache weight_cache;
    };
}
