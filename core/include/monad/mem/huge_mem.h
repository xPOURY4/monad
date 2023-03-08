#pragma once

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>

typedef struct huge_mem_t
{
    size_t size;
    void *data;
} huge_mem_t;

static_assert(sizeof(huge_mem_t) == 16);
static_assert(alignof(huge_mem_t) == 8);
