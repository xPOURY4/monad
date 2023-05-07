#include <CLI/CLI.hpp>
#include <alloca.h>
#include <assert.h>
#include <ethash/keccak.h>
#include <monad/mem/cpool.h>
#include <monad/merkle/tr.h>
#include <monad/trie/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cpool_31_t tmp_pool;
int fd;
int inflight;
int inflight_rd;
int n_rd_per_block;
struct io_uring *ring;
// write buffer info
unsigned char *write_buffer;
size_t buffer_idx;
int64_t block_off;

merkle_node_t *
read_node_from_disk(int64_t offset, unsigned char const node_path_len)
{
    unsigned char *buffer = get_avail_buffer(READ_BUFFER_SIZE);
    unsigned buffer_off = read_buffer_from_disk(fd, offset, buffer);
    merkle_node_t *node =
        deserialize_node_from_buffer(buffer + buffer_off, node_path_len);
    free(buffer);
    return node;
}

static inline merkle_node_t *
get_merkle_next(merkle_node_t *const node, unsigned int const child_idx)
{
    if (!node->children[child_idx].next) {
        node->children[child_idx].next = read_node_from_disk(
            node->children[child_idx].fnext,
            node->children[child_idx].path_len);
    }
    return node->children[child_idx].next;
}

int64_t count_db_size(merkle_node_t *node)
{
    int64_t cnt = 0;
    // recursion
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].fnext) {
            cnt += count_db_size(get_merkle_next(node, i));
            free_trie(node->children[i].next);
            node->children[i].next = NULL;
        }
        else { // leaf;
            ++cnt;
        }
    }
    return cnt;
}

int main(int argc, char *argv[])
{
    std::string dbname = "test.db";
    CLI::App cli{"monad_trie_perf_test"};
    cli.add_option("--db-name", dbname, "db file name");
    cli.parse(argc, argv);

    fd = tr_open(dbname.c_str());
    merkle_node_t *root = get_root_from_footer(fd);

    int64_t nkeys = count_db_size(root);
    printf("There are %ld nkeys in db %s\n", nkeys, dbname.c_str());
    return 0;
}
