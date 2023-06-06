#include <monad/core/nibble.h>

unsigned char *nibble_strcpy_disas(
    unsigned char *const restrict dest, unsigned char const *const restrict src)
{
    return nibble_strcpy(dest, src);
}

unsigned char *nibble_strcat_disas(
    unsigned char *const restrict dest, unsigned char const *const restrict src)
{
    return nibble_strcat(dest, src);
}
