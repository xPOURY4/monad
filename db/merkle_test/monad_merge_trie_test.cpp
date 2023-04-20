#include <CLI/CLI.hpp>
#include <alloca.h>
#include <assert.h>
#include <ethash/keccak.h>
#include <monad/mem/cpool.h>
#include <monad/mem/huge_mem.h>
#include <monad/merkle/commit.h>
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

/* magic numbers */
#define SLICE_LEN 100000
cpool_31_t tmp_pool;
int fd;
int n_pending_rq;
struct io_uring *ring;

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

/*  Commit one batch of updates
    offset: key offset, insert key starting from this number
    nkeys: number of keys to insert in this batch
*/
static merkle_node_t *batch_upsert_commit(
    merkle_node_t *prev_root, int64_t offset, int64_t nkeys,
    char const *const keccak_keys, char const *const keccak_values)
{
    struct timespec ts_before, ts_after;
    double tm_ram, tm_commit;
    // const int j = 1000;

    uint32_t tmp_root_i = get_new_branch(NULL, 0);

    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    for (int i = offset; i < offset + nkeys; ++i) {
        upsert(
            tmp_root_i,
            (unsigned char *)(keccak_keys + i * 32),
            64,
            (trie_data_t *)(keccak_values + i * 32));
        // if ((i + 1) % j == 0) {
        //     int first_byte = (int)*(keccak_keys + i * 32) & 0x00ff;
        //     erase(
        //         root,
        //         (unsigned char *)(keccak_keys + (i - first_byte) * 32),
        //         64);
        // }
    }
    merkle_node_t *new_root = do_merge(prev_root, get_node(tmp_root_i), 0);
    // after all merge call, also need to poll until no submission left in uring
    while (n_pending_rq) {
        poll_uring();
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_ram = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
             ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);

    // commit the tr to on-disk storage
    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    do_commit(fd, new_root);
    clock_gettime(CLOCK_MONOTONIC, &ts_after);

    tm_commit = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
                ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);

    off_t fsize = get_file_size(fd);

    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert/erase in RAM: %f "
        "/s, "
        "commit_t: %f "
        "s, total_t %.4f s\n",
        offset + nkeys,
        nkeys,
        (double)nkeys * 1.001 / tm_ram,
        tm_commit,
        tm_ram + tm_commit);
    fprintf(
        stdout,
        "db file size after commit: %f GB\n",
        float(fsize) / 1024 / 1024 / 1024);
    fflush(stdout);
    return new_root;
}

void prepare_keccak(
    size_t nkeys, size_t offset, char *const keccak_keys,
    char *const keccak_values)
{
    union ethash_hash256 hash;
    size_t i;
    size_t val;

    // prepare keccak
    for (i = offset; i < offset + nkeys; ++i) {
        // assign keccak256 on i to key
        hash = ethash_keccak256((const uint8_t *)&i, 8);
        memcpy(keccak_keys + i * 32, hash.str, 32);

        val = i * 2;
        hash = ethash_keccak256((const uint8_t *)&val, 8);
        memcpy(keccak_values + i * 32, hash.str, 32);
        // trie_data_t val_data;
        // val_data.words[0] = i + 1;
        // copy_trie_data((trie_data_t *)(keccak_values + i * 32), &val_data);
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
            offset + i * SLICE_LEN,
            keccak_keys,
            keccak_values));
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
    tmp_pool = *cpool_init31(tmp_huge_mem.data);

    // init io uring
    ring = (struct io_uring *)malloc(sizeof(struct io_uring));
    init_uring(ring);
    n_pending_rq = 0;

    int n_slices = 20;
    std::string dbname = "test.db";
    // bool append = false;
    int64_t offset = 0;
    CLI::App cli{"monad_trie_perf_test"};
    // cli.add_flag("--append", append, "append on top of existing db");
    cli.add_option("--db-name", dbname, "db file name");
    cli.add_option("--offset", offset, "integer offset to start insert");
    cli.add_option("-n", n_slices, "n batch updates");
    cli.parse(argc, argv);

    int64_t nkeys = SLICE_LEN * n_slices;
    char *const keccak_keys = (char *)malloc(nkeys * 32);
    char *const keccak_values = (char *)malloc(nkeys * 32);
    // pre-calculate keccak
    // prepare_keccak(nkeys, 0, keccak_keys, keccak_values);
    // spawn multiple threads for keccak
    prepare_keccak_parallel(nkeys, keccak_keys, keccak_values, offset);
    fprintf(stdout, "Finish preparing keccak.\nStart transactions\n");
    fflush(stdout);

    // create tr
    fd = tr_open(dbname.c_str());
    merkle_node_t *root = get_new_merkle_node(0);
    merkle_node_t *prev_root;
    /* start profiling upsert and commit */
    for (int iter = 0; iter < n_slices; ++iter) {
        prev_root = root;
        root = batch_upsert_commit(
            prev_root, iter * SLICE_LEN, SLICE_LEN, keccak_keys, keccak_values);
        // TODO: free prev root, need to make sure no multi-referencing in tries
    }

    tr_close(fd);
    exit_uring(ring);
    huge_mem_free(&tmp_huge_mem);
    free(keccak_keys);
    free(keccak_values);
}
