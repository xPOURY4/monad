#include <monad/core/bit.h>

size_t popcount_disas(size_t const x)
{
    return popcount(x);
}

bool has_single_bit_disas(size_t const x)
{
    return has_single_bit(x);
}

size_t countl_zero_undef_disas(size_t const x)
{
    return countl_zero_undef(x);
}

size_t countr_zero_undef_disas(size_t const x)
{
    return countr_zero_undef(x);
}

size_t bit_width_undef_disas(size_t const x)
{
    return bit_width_undef(x);
}

size_t bit_ceil_undef_disas(size_t const x)
{
    return bit_ceil_undef(x);
}

size_t bit_ceil_countr_undef_disas(size_t const x)
{
    return countr_zero_undef(bit_ceil_undef(x));
}
