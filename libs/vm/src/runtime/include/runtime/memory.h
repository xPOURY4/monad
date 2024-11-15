#include <runtime/types.h>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void mload(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *offset_ptr)
    {
        *result_ptr = ctx->mload(exit_ctx, *offset_ptr);
    }

    template <evmc_revision Rev>
    void mstore(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *value_ptr)
    {
        ctx->mstore(exit_ctx, *offset_ptr, *value_ptr);
    }

    template <evmc_revision Rev>
    void mstore8(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *value_ptr)
    {
        ctx->mstore8(exit_ctx, *offset_ptr, *value_ptr);
    }

    template <evmc_revision Rev>
    void msize(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->msize();
    }
}
