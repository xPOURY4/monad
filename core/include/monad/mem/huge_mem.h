#pragma once

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct huge_mem_t
{
    size_t size;
    unsigned char *data;
} huge_mem_t;

static_assert(sizeof(huge_mem_t) == 16);
static_assert(alignof(huge_mem_t) == 8);

void huge_mem_alloc(huge_mem_t *, size_t);
void huge_mem_free(huge_mem_t *);

#ifdef __cplusplus
}
#endif