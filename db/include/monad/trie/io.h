#pragma once

#include <errno.h>
#include <liburing.h>
#include <monad/trie/config.h>
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
#define URING_ENTRIES 128
#define SQ_THREAD_IDLE_MS UINT_MAX

unsigned char *get_avail_buffer(size_t size);

int write_buffer_to_disk(int fd, unsigned char *buffer);

unsigned
read_buffer_from_disk(int fd, int64_t const offset, unsigned char *buffer);

// io_uring
int init_uring(int fd, struct io_uring *ring, unsigned kcpu);

void exit_uring(struct io_uring *ring);

#ifdef __cplusplus
}
#endif

