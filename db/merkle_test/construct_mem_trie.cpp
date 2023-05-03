#include <CLI/CLI.hpp>
#include <alloca.h>
#include <assert.h>
#include <ethash/keccak.h>
#include <monad/mem/cpool.h>
#include <monad/mem/huge_mem.h>
#include <monad/merkle/node.h>
#include <monad/tmp/update.h>
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
cpool_31_t pool;
cpool_31_t tmp_pool;

static void ctrl_c_handler(int s)
{
    (void)s;
    exit(0);
}

// no commit
static uint32_t construct_in_mem_trie(
    int64_t offset, int64_t nkeys, char const *const keccak_keys,
    char const *const keccak_values)
{
    struct timespec ts_before, ts_after;
    double tm_ram;

    uint32_t tmp_root = get_new_branch(NULL, 0);

    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    for (int i = offset; i < offset + nkeys; ++i) {
        upsert(
            tmp_root,
            (unsigned char *)(keccak_keys + i * 32),
            64,
            (trie_data_t *)(keccak_values + i * 32));
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_ram = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
             ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);

    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert/erase in RAM: %f "
        "/s, time %f s\n",
        offset + nkeys,
        nkeys,
        (double)nkeys / tm_ram,
        tm_ram);
    fflush(stdout);
    return tmp_root;
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

void _keccak(trie_branch_node_t *const node)
{
    // node->data = keccak(concat(node->next[i]->data)) for i in [0,16)
    // if node->next[i]!= NULL
    if (node->type != BRANCH) {
        return;
    }
    unsigned char bytes[32 * 16];
    uint64_t b_offset = 0;
    int16_t k, subnode_mask = node->subnode_bitmask;
    while (subnode_mask) {
        k = __builtin_ctz(subnode_mask);
        copy_trie_data(
            (trie_data_t *)(bytes + b_offset), &get_node(node->next[k])->data);
        b_offset += 32;
        subnode_mask &= ~(1u << k);
    }
    copy_trie_data(
        &(node->data),
        (trie_data_t *)ethash_keccak256((uint8_t *)bytes, b_offset).str);
}

void _add(trie_branch_node_t *const node)
{
    // node->data = keccak(concat(node->next[i]->data)) for i in [0,16)
    // if node->next[i]!= NULL
    if (node->type != BRANCH) {
        return;
    }
    uint64_t data = 0;
    int16_t k, subnode_mask = node->subnode_bitmask;
    while (subnode_mask) {
        k = __builtin_ctz(subnode_mask);
        data += get_node(node->next[k])->data.words[0];
        subnode_mask &= ~(1u << k);
    }
    node->data.words[0] = data;
}

void precommit(uint32_t node_i, void (*compute)(trie_branch_node_t *const))
{
    // recompute updated node data from bottom up
    // commit: set fnext for subnodes
    trie_branch_node_t *node = get_node(node_i);
    if (node->type == LEAF) {
        return;
    }
    for (unsigned char i = 0; i < 16; ++i) {
        if (node->next[i]) {
            precommit(node->next[i], compute);
        }
    }
    assert(node_i != 0 && cpool_valid31(&pool, node_i) == true);
    compute(node);
}

int main(int argc, char *argv[])
{
    struct sigaction sig;
    sig.sa_handler = &ctrl_c_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGINT, &sig, NULL);

    int n_slices = 20;
    CLI::App cli{"monad_trie_perf_test"};
    cli.add_option("-n", n_slices, "updates");
    cli.parse(argc, argv);

    huge_mem_t tmp_huge_mem;
    tmp_huge_mem.data = NULL;
    tmp_huge_mem.size = 0;
    huge_mem_alloc(&tmp_huge_mem, 1UL << 31);
    tmp_pool = *cpool_init31(tmp_huge_mem.data);

    int64_t nkeys = SLICE_LEN * n_slices;
    char *const keccak_keys = (char *)malloc(nkeys * 32);
    char *const keccak_values = (char *)malloc(nkeys * 32);
    // pre-calculate keccak
    // prepare_keccak(nkeys, 0, keccak_keys, keccak_values);
    // spawn multiple threads for keccak
    prepare_keccak_parallel(nkeys, keccak_keys, keccak_values, 0);
    fprintf(stdout, "Finish preparing keccak.\nStart transactions\n");
    fflush(stdout);

    // create tr
    /* start profiling upsert and commit */
    // for (int iter = 0; iter < n_slices; ++iter) {
    //     construct_in_mem_trie(
    //         iter * SLICE_LEN, SLICE_LEN, keccak_keys, keccak_values);
    //     memset(tmp_huge_mem.data, 0, tmp_huge_mem.size);
    // }
    // test keccak result
    uint32_t root_i = construct_in_mem_trie(
        0, n_slices * SLICE_LEN, keccak_keys, keccak_values);
    precommit(root_i, _keccak);
    // precommit(root_i, _add);
    printf("root data.words[0] = 0x%lx\n", get_node(root_i)->data.words[0]);

    huge_mem_free(&tmp_huge_mem);
}
