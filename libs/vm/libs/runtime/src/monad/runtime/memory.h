#include <monad/runtime/transmute.h>
#include <monad/runtime/types.h>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void mload(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *offset_ptr)
    {
        auto offset = ctx->get_memory_offset(*offset_ptr);
        ctx->expand_memory(offset + bin<32>);
        *result_ptr = uint256_load_be(ctx->memory.data + *offset);
    }

    template <evmc_revision Rev>
    void mstore(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *value_ptr)
    {
        auto offset = ctx->get_memory_offset(*offset_ptr);
        ctx->expand_memory(offset + bin<32>);
        uint256_store_be(ctx->memory.data + *offset, *value_ptr);
    }

    template <evmc_revision Rev>
    void mstore8(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *value_ptr)
    {
        auto offset = ctx->get_memory_offset(*offset_ptr);
        ctx->expand_memory(offset + bin<1>);
        ctx->memory.data[*offset] = intx::as_bytes(*value_ptr)[0];
    }

    template <evmc_revision Rev>
    void mcopy(
        Context *ctx, utils::uint256_t const *dst_ptr,
        utils::uint256_t const *src_ptr, utils::uint256_t const *size_ptr)
    {
        auto size = ctx->get_memory_offset(*size_ptr);
        if (*size > 0) {
            auto src = ctx->get_memory_offset(*src_ptr);
            auto dst = ctx->get_memory_offset(*dst_ptr);
            ctx->expand_memory(max(dst, src) + size);
            auto size_in_words = shr_ceil<5>(size);
            ctx->deduct_gas(size_in_words * bin<3>);
            std::memmove(
                ctx->memory.data + *dst, ctx->memory.data + *src, *size);
        }
    }
}
