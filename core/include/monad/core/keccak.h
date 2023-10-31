#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

void keccak256(
    unsigned char const *in, unsigned long len, unsigned char out[32]);

#ifdef __cplusplus
}
#endif
