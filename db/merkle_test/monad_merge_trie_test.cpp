#include <CLI/CLI.hpp>
#include <cassert>
#include <ethash/keccak.h>
#include <sys/syscall.h> // for SYS_gettid
#include <unistd.h> // for syscall()

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

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>

#define MIN(x, y) x > y ? y : x
#define SLICE_LEN 100000

using namespace monad::trie;
using namespace monad::mpt;

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
inline void batch_upsert_commit(
    std::ostream &csv_writer, uint64_t block_id, int64_t keccak_offset,
    int64_t offset, int64_t nkeys, unsigned char *const keccak_keys,
    unsigned char *const keccak_values, bool erase, MerkleTrie &trie,
    index_t &index)
{
    double tm_ram;

    std::vector<Update> update_vec;
    update_vec.reserve(SLICE_LEN);
    UpdateList updates;
    for (int i = keccak_offset; i < keccak_offset + nkeys; ++i) {
        update_vec.push_back(Update{
            {keccak_keys + i * 32,
             erase ? std::nullopt
                   : std::optional<Data>{monad::byte_string_view(
                         keccak_values + i * 32, 32)}},
            UpdateMemberHook{}});
        updates.push_front(update_vec[i - keccak_offset]);
    }

    auto ts_before = std::chrono::steady_clock::now();

    int64_t root_off = trie.process_updates(updates);
    index.write_record(block_id, root_off);

    auto ts_after = std::chrono::steady_clock::now();
    tm_ram = std::chrono::duration_cast<std::chrono::nanoseconds>(
                 ts_after - ts_before)
                 .count() /
             1000000000.0;

    unsigned char root_data[32];
    trie.root_hash((unsigned char *)&root_data);

    fprintf(stdout, "root->data after precommit: ");
    __print_char_arr_in_hex((char *)root_data, 32);
    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert+pre+commit in "
        "RAM: "
        "%f /s, total_t %.4f s\n",
        offset + keccak_offset + nkeys,
        nkeys,
        (double)nkeys / tm_ram,
        tm_ram);
    fflush(stdout);
    if (csv_writer) {
        csv_writer << (offset + keccak_offset + nkeys) << ","
                   << ((double)nkeys / tm_ram) << std::endl;
    }
}

void prepare_keccak(
    size_t nkeys, unsigned char *const keccak_keys,
    unsigned char *const keccak_values, size_t idx_offset, size_t offset)
{
    size_t key;
    size_t val;

    // prepare keccak
    for (size_t i = idx_offset; i < idx_offset + nkeys; ++i) {
        // assign keccak256 on i to key
        key = i + offset;
        auto hash = ethash::keccak256((const uint8_t *)&key, 8);
        std::memcpy(keccak_keys + i * 32, hash.bytes, 32);

        val = key * 2;
        hash = ethash::keccak256((const uint8_t *)&val, 8);
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
    std::filesystem::path dbname_path = "test.db", csv_stats_path;
    int64_t offset = 0;
    unsigned sq_thread_cpu = 15;
    bool erase = false;
    CLI::App cli{"monad_merge_trie_test"};
    try {
        printf("main() runs on tid %ld\n", syscall(SYS_gettid));
        cli.add_flag("--append", append, "append on a specific version in db");
        cli.add_option("--vid", vid, "append on a specific version in db");
        cli.add_option("--db-name", dbname_path, "db file name");
        cli.add_option("--csv-stats", csv_stats_path, "CSV stats file name");
        cli.add_option("--offset", offset, "integer offset to start insert");
        cli.add_option("-n", n_slices, "n batch updates");
        cli.add_option("--kcpu", sq_thread_cpu, "io_uring sq_thread_cpu");
        cli.add_flag("--erase", erase, "test erase");
        cli.parse(argc, argv);

        std::ofstream csv_writer;
        if (!csv_stats_path.empty()) {
            csv_writer.open(csv_stats_path);
            csv_writer.exceptions(std::ios::failbit | std::ios::badbit);
            csv_writer << "\"Keys written\",\"Per second\"\n";
        }

        int64_t keccak_cap = 100 * SLICE_LEN;
        auto keccak_keys = std::make_unique<unsigned char[]>(keccak_cap * 32);
        auto keccak_values = std::make_unique<unsigned char[]>(keccak_cap * 32);

        // init uring
        monad::io::Ring ring(128, sq_thread_cpu);

        // init buffer
        monad::io::Buffers rwbuf{ring, 128, 128, 1UL << 13};

        Transaction trans(dbname_path);

        int fd = trans.get_fd();
        // init indexer
        index_t index(fd);

        // initialize root and block offset for write
        uint64_t block_off;
        merkle_node_t *root;
        if (append) {
            block_trie_info *trie_info = index.get_trie_info(vid);
            MONAD_ASSERT(trie_info->vid == vid);
            block_off = trie_info->root_off + MAX_DISK_NODE_SIZE;
            // blocking get_root
            root = read_node(fd, trie_info->root_off);
            ++vid;
        }
        else {
            block_off = index.get_start_offset();
            root = get_new_merkle_node(0, 0);
        }
        auto io =
            std::make_unique<AsyncIO>(ring, rwbuf, block_off, &update_callback);

        int fds[1] = {fd};
        io->uring_register_files(fds, 1);

        MerkleTrie trie(root, std::move(io));
        unsigned char root_data[32];
        trie.root_hash(root_data);
        fprintf(stdout, "root->data: ");
        __print_char_arr_in_hex((char *)root_data, 32);

        auto begin_test = std::chrono::steady_clock::now();
        int64_t max_key = n_slices * SLICE_LEN + offset;
        /* start profiling upsert and commit */
        for (int iter = 0; iter < n_slices; ++iter) {
            // renew keccak values
            if (!(iter * SLICE_LEN % keccak_cap)) {
                auto begin_prepare_keccak = std::chrono::steady_clock::now();
                if (iter) {
                    offset += keccak_cap;
                }
                // pre-calculate keccak
                prepare_keccak(
                    MIN(keccak_cap, max_key - offset),
                    keccak_keys.get(),
                    keccak_values.get(),
                    0,
                    offset);
                fprintf(
                    stdout, "Finish preparing keccak.\nStart transactions\n");
                fflush(stdout);
                auto end_prepare_keccak = std::chrono::steady_clock::now();
                begin_test += end_prepare_keccak - begin_prepare_keccak;
            }

            batch_upsert_commit(
                csv_writer,
                vid,
                (iter % 100) * SLICE_LEN,
                offset,
                SLICE_LEN,
                keccak_keys.get(),
                keccak_values.get(),
                false,
                trie,
                index);

            ++vid;

            if (erase && iter % 2) {
                fprintf(stdout, "> erase iter = %d\n", iter);
                fflush(stdout);
                batch_upsert_commit(
                    csv_writer,
                    vid,
                    (iter % 100) * SLICE_LEN,
                    offset,
                    SLICE_LEN,
                    keccak_keys.get(),
                    keccak_values.get(),
                    true,
                    trie,
                    index);
                ++vid;

                fprintf(stdout, "> dup batch iter = %d\n", iter);

                batch_upsert_commit(
                    csv_writer,
                    vid,
                    (iter % 100) * SLICE_LEN,
                    offset,
                    SLICE_LEN,
                    keccak_keys.get(),
                    keccak_values.get(),
                    false,
                    trie,
                    index);
                ++vid;
            }
        }

        free_trie(root);
        auto end_test = std::chrono::steady_clock::now();
        auto test_secs = std::chrono::duration_cast<std::chrono::microseconds>(
                             end_test - begin_test)
                             .count() /
                         1000000.0;
        printf("\nTotal test time: %f secs.\n", test_secs);
        if (csv_writer) {
            csv_writer << "\n\"Total test time:\"," << test_secs << std::endl;
        }
    }
    catch (const CLI::CallForHelp &e) {
        std::cout << cli.help() << std::flush;
    }
    catch (const std::exception &e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
