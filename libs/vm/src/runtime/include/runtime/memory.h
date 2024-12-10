#include <runtime/types.h>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void mload(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *offset_ptr)
    {
        *result_ptr = ctx->mload(*offset_ptr);
    }

    template <evmc_revision Rev>
    void mstore(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *value_ptr)
    {
        ctx->mstore(*offset_ptr, *value_ptr);
    }

    template <evmc_revision Rev>
    void mstore8(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *value_ptr)
    {
        ctx->mstore8(*offset_ptr, *value_ptr);
    }

    template <evmc_revision Rev>
    void mcopy(
        Context *ctx, utils::uint256_t const *dst, utils::uint256_t const *src,
        utils::uint256_t const *size)
    {
        ctx->mcopy(*dst, *src, *size);
    }
}
