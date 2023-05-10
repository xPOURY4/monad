#include <CLI/CLI.hpp>
#include <alloca.h>
#include <assert.h>
#include <ethash/keccak.h>
#include <monad/mem/cpool.h>
#include <monad/mem/huge_mem.h>
#include <monad/merkle/merge.h>
#include <monad/merkle/tr.h>
#include <monad/tmp/update.h>
#include <monad/trie/io.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <thread>
#include <time.h>
#include <vector>

#define MIN(x, y) x > y ? y : x

/* magic numbers */
#define SLICE_LEN 100000
cpool_31_t *tmp_pool;
int fd;
int inflight;
int inflight_rd;
int n_rd_per_block;
struct io_uring *ring;
// write buffer info
unsigned char *write_buffer;
size_t buffer_idx;
int64_t block_off;

static void ctrl_c_handler(int s)
{
    (void)s;
    exit(0);
}

off_t get_file_size(int fd)
{
    struct stat st;
    /* get file size */
    if (fstat(fd, &st) != 0) {
        perror("fstat failed.");
        exit(errno);
    }
    return st.st_size;
}

void __print_char_arr_in_hex(char *arr, int n)
{
    fprintf(stdout, "0x");
    for (int i = 0; i < n; ++i) {
        fprintf(stdout, "%02x", (uint8_t)arr[i]);
    }
    fprintf(stdout, "\n");
}

/*  Commit one batch of updates
    offset: key offset, insert key starting from this number
    nkeys: number of keys to insert in this batch
*/
static merkle_node_t *batch_upsert_commit(
    merkle_node_t *prev_root, int64_t keccak_offset, int64_t offset,
    int64_t nkeys, char const *const keccak_keys,
    char const *const keccak_values, bool erase)
{
    struct timespec ts_before, ts_middle, ts_after;
    double tm_ram, tm_io_extra;
    int inflight_before_poll = 0;
    int inflight_rd_before_poll = 0;
    n_rd_per_block = 0;

    uint32_t tmp_root_i = get_new_branch(NULL, 0);

    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    for (int i = keccak_offset; i < keccak_offset + nkeys; ++i) {
        upsert(
            tmp_root_i,
            (unsigned char *)(keccak_keys + i * 32),
            64,
            (trie_data_t *)(keccak_values + i * 32),
            erase);
    }
    // initialize buffer and block_off
    buffer_idx = 1;
    write_buffer = get_avail_buffer(WRITE_BUFFER_SIZE);
    *write_buffer = BLOCK_TYPE_DATA;
    // initialize tnode
    tnode_t *root_tnode = get_new_tnode(NULL, 0, 0, NULL);
    merkle_node_t *new_root =
        do_merge(prev_root, get_node(tmp_root_i), 0, root_tnode);
    // after all merge call, also need to poll until no submission left in uring
    clock_gettime(CLOCK_MONOTONIC, &ts_middle);
    inflight_before_poll = inflight;
    inflight_rd_before_poll = inflight_rd;

    while (inflight) {
        poll_uring();
    }
    // handle the last buffer to write
    if (buffer_idx > 1) {
        async_write_request(write_buffer, block_off);
        block_off += WRITE_BUFFER_SIZE;
    }
    else {
        free(write_buffer);
    }
    write_root_footer(new_root);
    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_ram = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
             ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);
    tm_io_extra = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
                  ((double)ts_middle.tv_sec + (double)ts_middle.tv_nsec / 1e9);

    assert(root_tnode->npending == 0);
    trie_data_t root_data;
    hash_branch(new_root, (unsigned char *)&root_data);

    // if ((offset + keccak_offset + nkeys) % (10 * SLICE_LEN)) {
    //     return new_root;
    // }
    fprintf(
        stdout,
        "inflight_before_poll = %d, "
        "inflight_rd_before_poll = %d, n_rd_per_block = %d\n"
        "number of unconsumed ready entries in CQ ring: %d\n"
        "extra time waiting for io %.4f s\ndb file size after commit: %f GB\n",
        inflight_before_poll,
        inflight_rd_before_poll,
        n_rd_per_block,
        io_uring_cq_ready(ring),
        tm_io_extra,
        float(get_file_size(fd)) / 1024 / 1024 / 1024);
    fprintf(stdout, "root->data after precommit: ");
    __print_char_arr_in_hex((char *)root_data.bytes, 32);
    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert+pre+commit in "
        "RAM: "
        "%f /s, total_t %.4f s\n",
        offset + keccak_offset + nkeys,
        nkeys,
        (double)nkeys * 1.001 / tm_ram,
        tm_ram);
    fflush(stdout);
    return new_root;
}

void prepare_keccak(
    size_t nkeys, char *const keccak_keys, char *const keccak_values,
    size_t idx_offset, size_t offset)
{
    union ethash_hash256 hash;
    size_t key;
    size_t val;

    // prepare keccak
    for (size_t i = idx_offset; i < idx_offset + nkeys; ++i) {
        // assign keccak256 on i to key
        key = i + offset;
        hash = ethash_keccak256((const uint8_t *)&key, 8);
        memcpy(keccak_keys + i * 32, hash.str, 32);

        val = key * 2;
        hash = ethash_keccak256((const uint8_t *)&val, 8);
        memcpy(keccak_values + i * 32, hash.str, 32);
    }
}

int prepare_keccak_parallel(
    size_t nkeys, char *const keccak_keys, char *const keccak_values,
    int64_t offset)
{
    int n_threads = nkeys / SLICE_LEN;
    std::vector<std::thread> threads;
    for (int i = 0; i < n_threads; ++i) {
        threads.push_back(std::thread(
            prepare_keccak,
            SLICE_LEN,
            keccak_keys,
            keccak_values,
            i * SLICE_LEN,
            offset));
    }
    for (int i = 0; i < n_threads; ++i) {
        threads[i].join();
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct sigaction sig;
    sig.sa_handler = &ctrl_c_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGINT, &sig, NULL);

    // init cpool
    huge_mem_t tmp_huge_mem;
    tmp_huge_mem.data = NULL;
    tmp_huge_mem.size = 0;
    huge_mem_alloc(&tmp_huge_mem, 1UL << 31);
    tmp_pool = cpool_init31(tmp_huge_mem.data);

    inflight = 0;
    inflight_rd = 0;

    int n_slices = 20;
    std::string dbname = "test.db";
    bool append = false;
    int64_t offset = 0;
    unsigned sq_thread_cpu = 15;
    bool erase = false;
    CLI::App cli{"monad_trie_perf_test"};
    cli.add_flag("--append", append, "append on top of existing db");
    cli.add_option("--db-name", dbname, "db file name");
    cli.add_option("--offset", offset, "integer offset to start insert");
    cli.add_option("-n", n_slices, "n batch updates");
    cli.add_option("--kcpu", sq_thread_cpu, "io_uring sq_thread_cpu");
    cli.add_flag("--erase", erase, "test erase");
    cli.parse(argc, argv);

    int64_t keccak_cap = 100 * SLICE_LEN;
    char *const keccak_keys = (char *)malloc(keccak_cap * 32);
    char *const keccak_values = (char *)malloc(keccak_cap * 32);
    // spawn multiple threads for keccak
    // prepare_keccak_parallel(nkeys, keccak_keys, keccak_values, offset);

    // create tr
    fd = tr_open(dbname.c_str());
    // init io uring
    ring = (struct io_uring *)malloc(sizeof(struct io_uring));
    int ret = init_uring(fd, ring, sq_thread_cpu);
    if (ret) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
        return 1;
    }
    // initialize root and block offset for write
    merkle_node_t *root;
    if (append) {
        // TODO: change block_off to support block device
        root = get_root_from_footer(fd);
        block_off = lseek(fd, 0, SEEK_END);
        trie_data_t root_data;
        hash_branch(root, (unsigned char *)&root_data);
        fprintf(
            stdout,
            "prev root->data[0] after precommit: 0x%lx\n",
            root_data.words[0]);
    }
    else {
        root = get_new_merkle_node(0, 0);
        block_off = 0;
    }

    merkle_node_t *prev_root;
    int64_t max_key = n_slices * SLICE_LEN + offset;
    /* start profiling upsert and commit */
    for (int iter = 0; iter < n_slices; ++iter) {
        // renew keccak values
        if (!(iter * SLICE_LEN % keccak_cap)) {
            if (iter) {
                offset += keccak_cap;
            }
            // pre-calculate keccak
            prepare_keccak(
                MIN(keccak_cap, max_key - offset),
                keccak_keys,
                keccak_values,
                0,
                offset);
            fprintf(stdout, "Finish preparing keccak.\nStart transactions\n");
            fflush(stdout);
        }

        prev_root = root;
        root = batch_upsert_commit(
            prev_root,
            (iter % 100) * SLICE_LEN,
            offset,
            SLICE_LEN,
            keccak_keys,
            keccak_values,
            false);
        free_trie(prev_root);

        if (erase && iter % 2) {
            fprintf(stdout, "> erase iter = %d\n", iter);
            fflush(stdout);
            prev_root = root;
            root = batch_upsert_commit(
                prev_root,
                (iter % 100) * SLICE_LEN,
                offset,
                SLICE_LEN,
                keccak_keys,
                keccak_values,
                true);
            free_trie(prev_root);

            fprintf(stdout, "> dup batch iter = %d\n", iter);
            prev_root = root;
            root = batch_upsert_commit(
                prev_root,
                (iter % 100) * SLICE_LEN,
                offset,
                SLICE_LEN,
                keccak_keys,
                keccak_values,
                false);
            free_trie(prev_root);
        }
    }

    while (inflight) {
        poll_uring();
    }
    assert(inflight == 0);
    assert(inflight_rd == 0);

    free_trie(root);
    tr_close(fd);
    exit_uring(ring);
    huge_mem_free(&tmp_huge_mem);
    free(keccak_keys);
    free(keccak_values);
}
