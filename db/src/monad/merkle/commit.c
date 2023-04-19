#include <errno.h>
#include <monad/merkle/commit.h>
#include <monad/merkle/node.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int write_buffer_to_disk(int fd, unsigned char *buffer)
{
    unsigned char *write_pos = buffer;
    size_t bytes_write = 0;
    for (;;) {
        ssize_t write_res;
        write_res = write(fd, write_pos, BUFFER_SIZE - bytes_write);
        if (write_res < 0) {
            printf("write fail fd %d, bytes_write = %lu.\n", fd, bytes_write);
            exit(errno);
        }
        bytes_write += write_res;
        if (bytes_write >= BUFFER_SIZE) {
            break;
        }
        write_pos += write_res;
    }
    free(buffer);
    return 0;
}

static void write_footer(int fd, int64_t root_offset)
{
    unsigned char *buffer = (unsigned char *)malloc(BUFFER_SIZE);
    buffer[0] = BLOCK_TYPE_META;
    *(int64_t *)(buffer + 8) = root_offset;
    if (write_buffer_to_disk(fd, buffer) < 0) {
        printf("error write_buffer_to_disk() for footer\n");
    }
}

void do_commit(int fd, merkle_node_t *const root)
{
    // get current disk offset from footer
    off_t block_off = lseek(fd, 0, SEEK_END);
    unsigned char *buffer = malloc(BUFFER_SIZE);
    buffer[0] = BLOCK_TYPE_DATA;
    size_t buffer_idx = 1;

    write_trie_retdata_t root_ret =
        write_trie(fd, &buffer, &buffer_idx, root, &block_off);

    if (write_buffer_to_disk(fd, buffer) < 0) {
        printf("error write_buffer_to_disk() for nodes\n");
    }
    // write footer with root_offset
    write_footer(fd, root_ret.fnext); // TODO: version info
    printf("root->data[0] after precommit: 0x%lx\n", root_ret.first_word_data);
    return;
}

// return the last node's file offset
// size_t* next_offset always shows the next available file offset to write to
// update: precommit (using add for now) before write, free trie if path_len > 5

write_trie_retdata_t write_trie(
    int fd, unsigned char **const buffer, size_t *buffer_idx,
    merkle_node_t *const node, int64_t *const block_off)
{
    // Write in a bottom up fashion
    uint64_t data = 0;
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].next && !node->children[i].fnext) {
            write_trie_retdata_t ret = write_trie(
                fd, buffer, buffer_idx, node->children[i].next, block_off);
            node->children[i].fnext = ret.fnext;
            node->children[i].data.words[0] = ret.first_word_data;
        }
        data += node->children[i].data.words[0];
    }

    size_t size = get_disk_node_size(node);
    if (size + *buffer_idx > BUFFER_SIZE) {
        // TODO: async write() in backend thread / io_uring
        if (write_buffer_to_disk(fd, *buffer) < 0) {
            perror("error write_buffer_to_disk() for nodes");
            exit(errno);
        }
        // renew buffer
        *block_off = *block_off + BUFFER_SIZE;
        *buffer = (unsigned char *)malloc(BUFFER_SIZE);
        **buffer = BLOCK_TYPE_DATA;
        *buffer_idx = 1;
    }
    // Write the root node to the buffer
    int64_t ret = *block_off + *buffer_idx;
    write_node_to_buffer(*buffer + *buffer_idx, node);
    *buffer_idx = *buffer_idx + size;

    return (write_trie_retdata_t){.fnext = ret, .first_word_data = data};
}
