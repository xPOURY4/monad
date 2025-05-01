#include <monad/vm/code.hpp>
#include <monad/vm/varcode_cache.hpp>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <cstdint>
#include <optional>

namespace monad::vm
{
    VarcodeCache::VarcodeCache(std::uint32_t max_cache_kb)
        : weight_cache{max_cache_kb}
    {
    }

    std::optional<Varcode>
    VarcodeCache::get(evmc_revision rev, evmc::bytes32 const &code_hash)
    {
        WeightCache::ConstAccessor acc;
        if (!weight_cache.find(acc, code_hash)) {
            return std::nullopt;
        }
        if (acc->second.value_.revision != rev) {
            return Varcode{acc->second.value_.varcode.intercode()};
        }
        return acc->second.value_.varcode;
    }

    void VarcodeCache::set(
        evmc_revision rev, evmc::bytes32 const &code_hash, Varcode const &vcode)
    {
        weight_cache.insert(code_hash, {rev, vcode});
    }
}
