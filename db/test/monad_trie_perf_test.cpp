#include "test_util.h"
#include <alloca.h>
#include <assert.h>
#include <ethash/keccak.h>
#include <monad/trie/nibble.h>
#include <monad/trie/node.h>
#include <monad/trie/update.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <time.h>
#include <vector>

/* magic numbers */
#define SLICE_LEN 1000000

static void ctrl_c_handler(int s)
{
    (void)s;
    exit(0);
}

/*  Commit one batch of updates
    offset: key offset, insert key starting from this number
    nkeys: number of keys to insert in this batch
*/
static void batch_upsert_commit(
    trie_branch_node_t *root, int64_t offset, int64_t nkeys, char *keccak_keys,
    char *keccak_values)
{
    struct timespec ts_before, ts_after;
    double tm_ram, tm_commit;
    const int j = 1000;

    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    for (int i = offset; i < offset + nkeys; i++) {
        upsert(
            root,
            (unsigned char *)(keccak_keys + i * 32),
            64,
            (trie_data_t *)(keccak_values + i * 32));

        if ((i + 1) % j == 0) {
            int first_byte = (int)*(keccak_keys + i * 32) & 0x00ff;
            erase(
                root,
                (unsigned char *)(keccak_keys + (i - first_byte) * 32),
                64);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_ram = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
             ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);

    // expect to be previous n_leaves - 1000
    int n_leaves = trie_metrics(root);
    fprintf(stdout, "There are %d leaves after upsert/erase\n", n_leaves);
    fflush(stdout);

    // commit the tr to on-disk storage
    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    do_commit(root);
    // do_commit_recursive(root);
    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_commit = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
                ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);

    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert/erase in RAM: %f /s, "
        "commit_t: %f "
        "s\n\n",
        offset + nkeys,
        nkeys,
        (double)nkeys * 1.001 / tm_ram,
        tm_commit);
    fflush(stdout);
}

void prepare_keccak(
    size_t nkeys, size_t offset, char *keccak_keys, char *keccak_values)
{
    union ethash_hash256 hash;
    size_t i, rand_val;

    // prepare keccak
    for (i = offset; i < offset + nkeys; i++) {
        // assign keccak256 on i to key
        hash = ethash_keccak256((const uint8_t *)&i, 8);
        memcpy(keccak_keys + i * 32, hash.str, 32);

        rand_val = rand();
        hash = ethash_keccak256((const uint8_t *)&rand_val, 8);
        memcpy(keccak_values + i * 32, hash.str, 32);
    }
}

int prepare_keccak_parallel(
    size_t nkeys, char *keccak_keys, char *keccak_values)
{
    int n_threads = nkeys / SLICE_LEN;
    std::vector<std::thread> threads;
    for (int i = 0; i < n_threads; i++) {
        threads.push_back(std::thread(
            prepare_keccak,
            SLICE_LEN,
            i * SLICE_LEN,
            keccak_keys,
            keccak_values));
    }
    for (int i = 0; i < n_threads; i++) {
        threads[i].join();
    }
    return 0;
}

int main()
{
    struct sigaction sig;
    sig.sa_handler = &ctrl_c_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGINT, &sig, NULL);

    int n_slices = 10;
    int64_t nkeys = SLICE_LEN * n_slices;
    char *keccak_keys = (char *)malloc(nkeys * 32);
    char *keccak_values = (char *)malloc(nkeys * 32);
    // pre-calculate keccak
    // prepare_keccak(nkeys, 0, keccak_keys, keccak_values);
    // spawn multiple threads for keccak
    prepare_keccak_parallel(nkeys, keccak_keys, keccak_values);

    fprintf(stdout, "Finish preparing keccak.\nStart transactions\n");
    fflush(stdout);
    // create tr
    trie_branch_node_t *root =
        (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    root->type = BRANCH;

    /* start profiling upsert and commit */
    for (int8_t iter = 0; iter < n_slices; iter++) {
        batch_upsert_commit(
            root, iter * SLICE_LEN, SLICE_LEN, keccak_keys, keccak_values);
        // copy root for the next transaction
        root = copy_node(root);
    }
}
