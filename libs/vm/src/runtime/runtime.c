#include <stdint.h>
#include <stdlib.h>

typedef struct
{
    uint64_t data[4];
} uint256_t;

uint256_t evm_add(uint256_t a, uint256_t b)
{
    (void)a;
    (void)b;
    abort();
}
