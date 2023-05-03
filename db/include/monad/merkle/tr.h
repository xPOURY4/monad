#pragma once

#include <errno.h>
#include <fcntl.h>
#include <monad/merkle/node.h>
#include <monad/trie/io.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

int inline tr_open(const char *const path)
{
    int flag = O_CREAT | O_RDWR | O_DIRECT;
    int fd = open(path, flag, 0777); // TODO: configurable flag
    if (fd < 0) {
        perror("Fail to open the file.");
        exit(errno);
    }
    return fd;
}

void inline tr_close(int const fd)
{
    close(fd);
}

void write_root_footer(merkle_node_t *root)
{
    unsigned char *buffer = get_avail_buffer(WRITE_BUFFER_SIZE);
    buffer[0] = BLOCK_TYPE_META;
    serialize_node_to_buffer(buffer + 1, root);
    async_write_request(buffer, block_off);
    block_off += WRITE_BUFFER_SIZE;
}

merkle_node_t *get_root_from_footer(int fd)
{
    struct stat st;
    /* get file size */
    if (fstat(fd, &st) != 0) {
        perror("fstat failed.");
        exit(errno);
    }
    // TODO: optimize it with bitop
    int tmp_bit = __builtin_ctz(WRITE_BUFFER_SIZE);
    int64_t aligned_off = (st.st_size >> tmp_bit) << tmp_bit;
    unsigned char *buffer;
    while (1) {
        aligned_off -= WRITE_BUFFER_SIZE;
        int buffer_off =
            read_buffer_from_disk(fd, aligned_off, &buffer, MAX_DISK_NODE_SIZE);
        assert(buffer_off == 0);
        // check buffer type
        if (*(buffer + buffer_off) == BLOCK_TYPE_META) {
            break;
        }
        printf(" *(buffer + buffer_off) %u ", *(buffer + buffer_off));
        free(buffer);
    }
    // get root
    merkle_node_t *root = deserialize_node_from_buffer(buffer + 1);
    free(buffer);
    return root;
}

#ifdef __cplusplus
}
#endif
