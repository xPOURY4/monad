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
    void msize(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->msize();
    }
}
