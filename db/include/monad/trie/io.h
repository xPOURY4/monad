#pragma once

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ALIGNMENT 512
#define WRITE_BUFFER_SIZE 64 * 1024
#define READ_BUFFER_SIZE 2048

unsigned char *get_avail_buffer(size_t size);

int write_buffer_to_disk(int fd, unsigned char *buffer);

unsigned read_buffer_from_disk(
    int fd, int64_t const offset, unsigned char **buffer, size_t size);

#ifdef __cplusplus
}
#endif