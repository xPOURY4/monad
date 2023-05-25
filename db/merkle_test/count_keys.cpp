#include <CLI/CLI.hpp>
#include <alloca.h>
#include <assert.h>
#include <ethash/keccak.h>
#include <monad/mem/cpool.h>
#include <monad/trie/io.h>
#include <monad/trie/tr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cpool_31_t *tmp_pool;
int fd;
int inflight;
int inflight_rd;
int n_rd_per_block;
struct io_uring *ring;
// write buffer info
unsigned char *WRITE_BUFFER;
size_t BUFFER_IDX;
int64_t BLOCK_OFF;
int64_t cnt;

typedef struct read_uring_data_t
{
    merkle_node_t *parent;
    unsigned char *buffer;
    unsigned buffer_off;
    uint8_t child_idx;
    uint8_t node_path_len;
} read_uring_data_t;
static_assert(sizeof(read_uring_data_t) == 24);
static_assert(alignof(read_uring_data_t) == 8);

void count_db_size(merkle_node_t *node);
void poll_();
void async_read_request_for_count(
    int64_t offset, unsigned char const node_path_len);

void poll_()
{
    struct io_uring_cqe *cqe;
    // get one completed request
    int ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "io_uring_wait_cqe fail: %s\n", strerror(-ret));
        exit(1);
    }
    if (cqe->res < 0) {
        /* The system call invoked asynchonously failed */
        fprintf(stderr, "async syscall failed: %s\n", strerror(-cqe->res));
        // TODO: resubmit the request
        exit(1);
    }
    --inflight;

    // get data from io_uring
    void *uring_data = io_uring_cqe_get_data(cqe);
    if (!uring_data) {
        fprintf(stderr, "Error getting user data from CQE\n");
        exit(1);
    }
    read_uring_data_t *data = (read_uring_data_t *)uring_data;
    io_uring_cqe_seen(ring, cqe);

    merkle_node_t *node = deserialize_node_from_buffer(
        data->buffer + data->buffer_off, data->node_path_len);
    free(data->buffer);
    assert(node->nsubnodes);
    assert(node->mask);

    count_db_size(node);
    free(node);
    free(uring_data);
}

void async_read_request_for_count(
    int64_t offset, unsigned char const node_path_len)
{
    while (inflight >= URING_ENTRIES) {
        poll_();
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(
            stderr,
            "Could not get SQE for read, io_uring_sq_space_left = %d.\n",
            io_uring_sq_space_left(ring));
        exit(1);
    }
    // prep uring data
    read_uring_data_t *uring_data =
        (read_uring_data_t *)malloc(sizeof(read_uring_data_t));
    // get_read_buffer, buffer_off, and read_size
    int64_t off_aligned = (offset >> 9) << 9;
    uint16_t buffer_off = (uint16_t)(offset - off_aligned);
    size_t read_size = READ_BUFFER_SIZE;
    unsigned char *rd_buffer = get_avail_buffer(read_size);

    uring_data->buffer = rd_buffer;
    uring_data->buffer_off = buffer_off;
    uring_data->node_path_len = node_path_len;

    // prep for read
    io_uring_prep_read(sqe, 0, rd_buffer, read_size, off_aligned);
    sqe->flags |= IOSQE_FIXED_FILE;
    // submit an io_uring request, with merge_params data
    io_uring_sqe_set_data(sqe, uring_data);
    io_uring_submit(ring);
    ++inflight;
}

void count_db_size(merkle_node_t *node)
{
    // recursion
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].fnext) {
            if (!node->children[i].next) {
                async_read_request_for_count(
                    node->children[i].fnext, node->children[i].path_len);
            }
            else {
                count_db_size(node->children[i].next);
                free_trie(node->children[i].next);
                node->children[i].next = NULL;
            }
        }
        else { // leaf;
            ++cnt;
            if (!cnt % 100000000) {
                printf("scanned %ld keys in db\n", cnt);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    std::string dbname = "test.db";
    CLI::App cli{"monad_trie_perf_test"};
    cli.add_option("--db-name", dbname, "db file name");
    cli.parse(argc, argv);

    fd = tr_open(dbname.c_str());
    merkle_node_t *root = get_root_from_footer(fd);
    cnt = 0;
    inflight = 0;
    // init io uring
    ring = (struct io_uring *)malloc(sizeof(struct io_uring));
    int ret = init_uring(fd, ring, 15);
    if (ret) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
        return 1;
    }
    count_db_size(root);

    while (inflight) {
        poll_();
    }
    printf("There are %ld nkeys in db %s\n", cnt, dbname.c_str());
    return 0;
}
