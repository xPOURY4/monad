#include <CLI/CLI.hpp>
#include <assert.h>
#include <ethash/keccak.h>

#include <boost/pool/object_pool.hpp>

#include <monad/core/byte_string.hpp>

#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>
#include <monad/mem/cpool.h>
#include <monad/mem/huge_mem.hpp>

#include <monad/mpt/update.hpp>
#include <monad/trie/index.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/node_helper.hpp>
#include <monad/trie/tr.hpp>
#include <monad/trie/trie.hpp>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN(x, y) x > y ? y : x
#define SLICE_LEN 100000

using namespace monad::trie;
using namespace monad::mpt;

MONAD_TRIE_NAMESPACE_BEGIN
cpool_29_t *tmppool_;
boost::object_pool<Request> request_pool;
MONAD_TRIE_NAMESPACE_END

static void ctrl_c_handler(int s)
{
    (void)s;
    exit(0);
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
    uint64_t block_id, merkle_node_t *prev_root, int64_t keccak_offset,
    int64_t offset, int64_t nkeys, unsigned char *const keccak_keys,
    unsigned char *const keccak_values, bool erase, AsyncIO &io, Index &index)
{
    struct timespec ts_before, ts_after;
    double tm_ram;

    std::vector<Update> update_vec;
    update_vec.reserve(SLICE_LEN);
    UpdateList updates;
    for (int i = keccak_offset; i < keccak_offset + nkeys; ++i) {
        update_vec.push_back(Update{
            {keccak_keys + i * 32,
             erase ? std::nullopt
                   : std::optional<Data>{byte_string_view(
                         keccak_values + i * 32, 32)}},
            UpdateMemberHook{}});
        updates.push_front(update_vec[i - keccak_offset]);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_before);

    MerkleTrie trie;
    trie.process_updates(block_id, updates, prev_root, io, index);

    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_ram = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
             ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);

    trie_data_t root_data;
    trie.root_hash((unsigned char *)&root_data);

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

    free_trie(prev_root);
    return trie.get_root();
}

void prepare_keccak(
    size_t nkeys, unsigned char *const keccak_keys,
    unsigned char *const keccak_values, size_t idx_offset, size_t offset)
{
    union ethash_hash256 hash;
    size_t key;
    size_t val;

    // prepare keccak
    for (size_t i = idx_offset; i < idx_offset + nkeys; ++i) {
        // assign keccak256 on i to key
        key = i + offset;
        hash = ethash_keccak256((const uint8_t *)&key, 8);
        std::memcpy(keccak_keys + i * 32, hash.str, 32);

        val = key * 2;
        hash = ethash_keccak256((const uint8_t *)&val, 8);
        std::memcpy(keccak_values + i * 32, hash.str, 32);
    }
}

int main(int argc, char *argv[])
{
    struct sigaction sig;
    sig.sa_handler = &ctrl_c_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGINT, &sig, nullptr);

    int n_slices = 20;
    bool append = false;
    uint64_t vid = 0;
    std::string dbname = "test.db";
    int64_t offset = 0;
    unsigned sq_thread_cpu = 15;
    bool erase = false;
    CLI::App cli{"monad_merge_trie_test"};
    cli.add_flag("--append", append, "append on a specific version in db");
    cli.add_option("--vid", vid, "append on a specific version in db");
    cli.add_option("--db-name", dbname, "db file name");
    cli.add_option("--offset", offset, "integer offset to start insert");
    cli.add_option("-n", n_slices, "n batch updates");
    cli.add_option("--kcpu", sq_thread_cpu, "io_uring sq_thread_cpu");
    cli.add_flag("--erase", erase, "test erase");
    cli.parse(argc, argv);

    int64_t keccak_cap = 100 * SLICE_LEN;
    unsigned char *const keccak_keys =
        (unsigned char *)std::malloc(keccak_cap * 32);
    unsigned char *const keccak_values =
        (unsigned char *)std::malloc(keccak_cap * 32);

    // init tmppool_
    monad::HugeMem tmp_huge_mem(1UL << 29);
    tmppool_ = cpool_init29(tmp_huge_mem.get_data());

    // init uring
    monad::io::Ring ring(128, sq_thread_cpu);

    // init buffer
    monad::io::Buffers rwbuf{ring, 128, 128, 1UL << 13};

    Transaction trans(dbname.c_str());

    // init indexer
    Index index(trans.get_fd());

    // initialize root and block offset for write
    merkle_node_t *prev_root, *root;
    uint64_t block_off = index.get_start_offset();
    if (append) {
        block_trie_info *trie_info = index.get_trie_info(vid);
        MONAD_ASSERT(trie_info->vid == vid);
        block_off = trie_info->root_off + MAX_DISK_NODE_SIZE;
        // blocking get_root
        root = read_node(trans.get_fd(), trie_info->root_off);

        trie_data_t root_data;
        encode_branch(root, (unsigned char *)&root_data);
        fprintf(
            stdout,
            "version %lu, root_off %lu, root->data after precommit: ",
            trie_info->vid,
            trie_info->root_off);
        __print_char_arr_in_hex((char *)root_data.bytes, 32);
        ++vid;
    }
    else {
        root = get_new_merkle_node(0, 0);
    }
    AsyncIO io(ring, rwbuf, block_off, tmppool_, &update_callback);

    int fds[1] = {trans.get_fd()};
    io.uring_register_files(fds, 1);

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
            vid,
            prev_root,
            (iter % 100) * SLICE_LEN,
            offset,
            SLICE_LEN,
            keccak_keys,
            keccak_values,
            false,
            io,
            index);

        ++vid;

        if (erase && iter % 2) {
            fprintf(stdout, "> erase iter = %d\n", iter);
            fflush(stdout);
            prev_root = root;
            root = batch_upsert_commit(
                vid,
                prev_root,
                (iter % 100) * SLICE_LEN,
                offset,
                SLICE_LEN,
                keccak_keys,
                keccak_values,
                true,
                io,
                index);
            ++vid;

            fprintf(stdout, "> dup batch iter = %d\n", iter);
            prev_root = root;

            root = batch_upsert_commit(
                vid,
                prev_root,
                (iter % 100) * SLICE_LEN,
                offset,
                SLICE_LEN,
                keccak_keys,
                keccak_values,
                false,
                io,
                index);
            ++vid;
        }
    }

    free_trie(root);
    free(keccak_keys);
    free(keccak_values);
}
