#include <monad/trie/offset.h>

void disas_off48_from_int(off48_t *const result, int64_t const *const offset)
{
    *result = off48_from_int(*offset);
}

void disas_off48_to_int(int64_t *const result, off48_t const *const offset)
{
    *result = off48_to_int(*offset);
}
