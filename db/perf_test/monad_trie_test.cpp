#include <CLI/CLI.hpp>
#include <cassert>
#include <sys/syscall.h> // for SYS_gettid
#include <unistd.h> // for syscall()

#include <monad/core/byte_string.hpp>
#include <monad/core/keccak.h>

#include <monad/mem/cpool.h>
#include <monad/mem/huge_mem.hpp>

#include <monad/mpt/compute.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/update.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

#define SLICE_LEN 100000

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
inline Node *batch_upsert_commit(
    std::ostream &csv_writer, uint64_t block_id, uint64_t keccak_offset,
    uint64_t offset, uint64_t nkeys,
    std::vector<monad::byte_string> &keccak_keys,
    std::vector<monad::byte_string> &keccak_values, bool erase,
    Node *prev_state_root, Compute &comp)
{
    // TODO: find state_root using find(root, block_id);
    (void)block_id;

    double tm_ram;

    std::vector<Update> update_vec;
    update_vec.reserve(SLICE_LEN);
    UpdateList update_ls;
    for (uint64_t i = keccak_offset; i < keccak_offset + nkeys; ++i) {
        update_vec.push_back(
            erase ? make_erase(keccak_keys[i])
                  : make_update(keccak_keys[i], keccak_values[i]));
        update_ls.push_front(update_vec[i - keccak_offset]);
    }
    auto ts_before = std::chrono::steady_clock::now();

    Node *new_node = upsert(comp, prev_state_root, std::move(update_ls));

    auto ts_after = std::chrono::steady_clock::now();
    tm_ram = std::chrono::duration_cast<std::chrono::nanoseconds>(
                 ts_after - ts_before)
                 .count() /
             1000000000.0;

    // fprintf(stdout, "root->data : ");
    // __print_char_arr_in_hex((char *), 32);
    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert+commit in "
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

    // fprintf(stdout, "num of leaves %u\n", count_leaves(root_container.next));
    return new_node;
}

void prepare_keccak(
    size_t nkeys, std::vector<monad::byte_string> &keccak_keys,
    std::vector<monad::byte_string> &keccak_values, size_t idx_offset,
    size_t offset)
{
    size_t key;
    size_t val;

    // prepare keccak
    for (size_t i = idx_offset; i < idx_offset + nkeys; ++i) {
        // assign keccak256 on i to key
        key = i + offset;
        keccak_keys[i].resize(32);
        keccak256((const unsigned char *)&key, 8, keccak_keys[i].data());

        val = key * 2;
        keccak_values[i].resize(32);
        keccak256((const unsigned char *)&val, 8, keccak_values[i].data());
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
    uint64_t offset = 0;
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

        uint64_t keccak_cap = 100 * SLICE_LEN;
        auto keccak_keys = std::vector<monad::byte_string>{keccak_cap};
        auto keccak_values = std::vector<monad::byte_string>{keccak_cap};

        Node *state_root = nullptr;
        EmptyCompute comp{};

        auto begin_test = std::chrono::steady_clock::now();
        uint64_t max_key = n_slices * SLICE_LEN + offset;
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
                    std::min(keccak_cap, max_key - int64_t(offset)),
                    keccak_keys,
                    keccak_values,
                    0,
                    offset);
                fprintf(
                    stdout, "Finish preparing keccak.\nStart transactions\n");
                fflush(stdout);
                auto end_prepare_keccak = std::chrono::steady_clock::now();
                begin_test += end_prepare_keccak - begin_prepare_keccak;
            }

            state_root = batch_upsert_commit(
                csv_writer,
                vid,
                (iter % 100) * SLICE_LEN,
                offset,
                SLICE_LEN,
                keccak_keys,
                keccak_values,
                false,
                state_root,
                comp);

            // if (erase && iter % 2) {
            //     fprintf(stdout, "> erase iter = %d\n", iter);
            //     fflush(stdout);
            //     state_root = batch_upsert_commit(
            //         csv_writer,
            //         vid,
            //         (iter % 100) * SLICE_LEN,
            //         offset,
            //         SLICE_LEN,
            //         keccak_keys.get(),
            //         keccak_values.get(),
            //         true,
            //         state_root,
            //         comp);
            //     ++vid;

            //     fprintf(stdout, "> dup batch iter = %d\n", iter);

            //     state_root = batch_upsert_commit(
            //         csv_writer,
            //         vid,
            //         (iter % 100) * SLICE_LEN,
            //         offset,
            //         SLICE_LEN,
            //         keccak_keys.get(),
            //         keccak_values.get(),
            //         false,
            //         state_root,
            //         comp);
            //     ++vid;
            // }

            ++vid;
        }

        auto end_test = std::chrono::steady_clock::now();
        auto test_secs = std::chrono::duration_cast<std::chrono::microseconds>(
                             end_test - begin_test)
                             .count() /
                         1000000.0;
        printf("\nTotal test time: %f secs.\n", test_secs);
        if (csv_writer) {
            csv_writer << "\n\"Total test time:\"," << test_secs << std::endl;
        }

        free_trie(state_root);
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
