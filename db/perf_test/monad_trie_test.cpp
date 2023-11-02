#include <CLI/CLI.hpp>
#include <cassert>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h> // for SYS_gettid
#include <unistd.h> // for syscall()

#include <monad/async/detail/scope_polyfill.hpp>

#include <monad/core/byte_string.hpp>
#include <monad/core/keccak.h>
#include <monad/core/small_prng.hpp>

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

void __print_bytes_in_hex(monad::byte_string_view arr)
{
    fprintf(stdout, "0x");
    for (auto &c : arr) {
        fprintf(stdout, "%02x", (uint8_t)c);
    }
    fprintf(stdout, "\n");
}

// helper traversal count
inline unsigned count_leaves(Node *const root, unsigned n = 0)
{
    if (!root) {
        return 0;
    }
    if (!bitmask_count(root->mask)) {
        return 1;
    }
    for (unsigned j = 0; j < bitmask_count(root->mask); ++j) {
        n += count_leaves(root->next_j(j));
    }
    return n;
}

/*  Commit one batch of updates
    key_offset: insert key starting from this number
    nkeys: number of keys to insert in this batch
*/
inline node_ptr batch_upsert_commit(
    std::ostream &csv_writer, uint64_t block_id, uint64_t vec_idx,
    uint64_t key_offset, uint64_t nkeys,
    std::vector<monad::byte_string> &keccak_keys,
    std::vector<monad::byte_string> &keccak_values, bool erase,
    node_ptr prev_root, UpdateAux &aux, TrieStateMachine &sm)
{
    auto const block_no = serialise_as_big_endian<6>(block_id);
    if (block_id != 0) {
        auto old_block_no = serialise_as_big_endian<6>(block_id - 1);
        prev_root = monad::mpt::copy_node(
            aux, std::move(prev_root), old_block_no, block_no);
        // For test purpose only: verify that earlier blocks are valid, no
        // change in db
        auto [state_root, res] = find_blocking(
            aux.is_on_disk() ? &aux.io->storage_pool() : nullptr,
            prev_root.get(),
            old_block_no);
        MONAD_ASSERT(res == find_result::success);
        MONAD_ASSERT(state_root->hash_len == 32);
    }

    double tm_ram;
    std::vector<Update> update_vec;
    update_vec.reserve(SLICE_LEN);
    UpdateList state_updates;
    for (uint64_t i = 0; i < nkeys; ++i) {
        update_vec.push_back(
            erase ? make_erase(keccak_keys[i + vec_idx])
                  : make_update(
                        keccak_keys[i + vec_idx], keccak_values[i + vec_idx]));
        state_updates.push_front(update_vec[i]);
    }
    Update u = make_update(block_no, {}, false, std::move(state_updates));
    UpdateList updates;
    updates.push_front(u);

    auto ts_before = std::chrono::steady_clock::now();
    node_ptr new_root = upsert(aux, sm, prev_root.get(), std::move(updates));
    auto ts_after = std::chrono::steady_clock::now();
    tm_ram = static_cast<double>(
                 std::chrono::duration_cast<std::chrono::nanoseconds>(
                     ts_after - ts_before)
                     .count()) /
             1000000000.0;

    auto [state_root, res] = find_blocking(
        aux.is_on_disk() ? &aux.io->storage_pool() : nullptr,
        new_root.get(),
        block_no);
    MONAD_ASSERT(res == find_result::success);
    fprintf(stdout, "root->data : ");
    __print_bytes_in_hex(state_root->hash_view());

    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert+commit in "
        "RAM: "
        "%f /s, total_t %.4f s\n",
        key_offset + vec_idx + nkeys,
        nkeys,
        (double)nkeys / tm_ram,
        tm_ram);
    fflush(stdout);
    if (csv_writer) {
        csv_writer << (key_offset + vec_idx + nkeys) << ","
                   << ((double)nkeys / tm_ram) << std::endl;
    }

    return new_root;
}

void prepare_keccak(
    size_t nkeys, std::vector<monad::byte_string> &keccak_keys,
    std::vector<monad::byte_string> &keccak_values, size_t key_offset)
{
    size_t key;
    size_t val;

    // prepare keccak
    for (size_t i = 0; i < nkeys; ++i) {
        // assign keccak256 on i to key
        key = i + key_offset;
        keccak_keys[i].resize(32);
        keccak256((unsigned char const *)&key, 8, keccak_keys[i].data());

        val = key * 2;
        keccak_values[i].resize(32);
        keccak256((unsigned char const *)&val, 8, keccak_values[i].data());
    }
}

int main(int argc, char *argv[])
{
    struct sigaction sig;
    sig.sa_handler = &ctrl_c_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGINT, &sig, nullptr);

    unsigned n_slices = 20;
    bool append = false;
    uint64_t block_no = 0;
    std::filesystem::path dbname_path = "test.db", csv_stats_path;
    uint64_t key_offset = 0;
    unsigned sq_thread_cpu = 15;
    bool erase = false;
    bool in_memory = false; // default is on disk
    bool empty_cpu_caches = false;

    // TODO: add block num in trie update, cache level should be 1,
    CLI::App cli{"monad_merge_trie_test"};
    try {
        printf("main() runs on tid %ld\n", syscall(SYS_gettid));
        cli.add_flag("--append", append, "append at a specific block in db");
        cli.add_option(
            "--block-no",
            block_no,
            "start at a specific block_no, append to block_no-1");
        cli.add_option("--db-name", dbname_path, "db file name");
        cli.add_option("--csv-stats", csv_stats_path, "CSV stats file name");
        cli.add_option(
            "--key-offset", key_offset, "integer offset to start insert");
        cli.add_option("-n", n_slices, "n batch updates");
        cli.add_option("--kcpu", sq_thread_cpu, "io_uring sq_thread_cpu");
        cli.add_flag("--erase", erase, "test erase");
        cli.add_flag(
            "--in-memory", in_memory, "config trie to in memory or on-disk");
        cli.add_flag(
            "--empty-cpu-caches",
            empty_cpu_caches,
            "empty cpu caches between updates");
        cli.parse(argc, argv);

        MONAD_ASSERT(in_memory + append < 2);

        std::ofstream csv_writer;
        if (!csv_stats_path.empty()) {
            csv_writer.open(csv_stats_path);
            csv_writer.exceptions(std::ios::failbit | std::ios::badbit);
            csv_writer << "\"Keys written\",\"Per second\"\n";
        }

        /* This does a good job of emptying the CPU's data caches and
        data TLB. It does not empty instruction caches nor instruction TLB,
        including the branch prediction history.
        */
        struct cpu_cache_emptier_t
        {
            enum : size_t
            {
                TLB_ENTRIES = 4096
            };
            std::vector<std::byte *> pages;
            ::monad::small_prng rand;
            explicit cpu_cache_emptier_t(bool enable)
            {
                if (enable) {
                    pages.resize(TLB_ENTRIES);
                    for (size_t n = 0; n < TLB_ENTRIES; n++) {
                        pages[n] = (std::byte *)::mmap(
                            0,
                            4096,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                            -1,
                            0);
                        *pages[n] = std::byte(1);
                    }
                }
            }
            ~cpu_cache_emptier_t()
            {
                for (auto *p : pages) {
                    ::munmap(p, 4096);
                }
            }
            void operator()()
            {
                for (size_t n = 0; n < pages.size() * 4; n++) {
                    auto const v = rand();
                    auto const idx1 = (v & 0xffff) & (TLB_ENTRIES - 1);
                    auto const idx2 = ((v >> 16) & 0xffff) & (TLB_ENTRIES - 1);
                    if (idx1 != idx2) {
                        memcpy(pages[idx1], pages[idx2], 4096);
                    }
                }
            }
        } cpu_cache_emptier{empty_cpu_caches};

        uint64_t const keccak_cap = 100 * SLICE_LEN;
        auto keccak_keys = std::vector<monad::byte_string>{keccak_cap};
        auto keccak_values = std::vector<monad::byte_string>{keccak_cap};

        if (!std::filesystem::exists(dbname_path)) {
            int fd =
                ::open(dbname_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (-1 == fd) {
                throw std::system_error(errno, std::system_category());
            }
            auto unfd =
                monad::make_scope_exit([fd]() noexcept { ::close(fd); });
            if (-1 ==
                ::ftruncate(
                    fd, 1ULL * 1024 * 1024 * 1024 * 1024 + 24576 /* 1Tb */)) {
                throw std::system_error(errno, std::system_category());
            }
        }
        MONAD_ASYNC_NAMESPACE::storage_pool pool{
            {&dbname_path, 1},
            append ? MONAD_ASYNC_NAMESPACE::storage_pool::mode::open_existing
                   : MONAD_ASYNC_NAMESPACE::storage_pool::mode::truncate};

        // init uring
        monad::io::Ring ring(128, sq_thread_cpu);

        // init buffer: default buffer size
        // TODO: pass in a preallocated memory
        monad::io::Buffers rwbuf{
            ring,
            8192 * 16,
            128,
            MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
            MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};

        auto io = MONAD_ASYNC_NAMESPACE::AsyncIO{pool, ring, rwbuf};

        UpdateAux aux{};
        StateMachineWithBlockNo sm{};
        if (!in_memory) {
            aux.set_io(&io);
        }

        node_ptr state_root{};
        if (append) {
            auto root_off = aux.get_root_offset();
            Node *root = read_node_blocking(io.storage_pool(), root_off);
            state_root.reset(root);

            chunk_offset_t block_start{0, 0};
            block_start = round_up_align<DISK_PAGE_BITS>(
                root_off.add_to_offset(root->get_disk_size()));
            // destroy contents after block_start.id chunck
            aux.rewind_root_offset_to(block_start);
        }

        auto begin_test = std::chrono::steady_clock::now();
        uint64_t max_key = n_slices * SLICE_LEN + key_offset;
        /* start profiling upsert and commit */
        for (uint64_t iter = 0; iter < n_slices; ++iter) {
            // renew keccak values
            if (!(iter * SLICE_LEN % keccak_cap)) {
                auto begin_prepare_keccak = std::chrono::steady_clock::now();
                if (iter) {
                    key_offset += keccak_cap;
                }
                // pre-calculate keccak
                prepare_keccak(
                    std::min(keccak_cap, max_key - key_offset),
                    keccak_keys,
                    keccak_values,
                    key_offset);
                fprintf(
                    stdout, "Finish preparing keccak.\nStart transactions\n");
                fflush(stdout);
                auto end_prepare_keccak = std::chrono::steady_clock::now();
                begin_test += end_prepare_keccak - begin_prepare_keccak;
            }

            cpu_cache_emptier();
            state_root = batch_upsert_commit(
                csv_writer,
                block_no++,
                (iter % 100) * SLICE_LEN, /* vec_idx */
                key_offset, /* key offset */
                SLICE_LEN, /* nkeys */
                keccak_keys,
                keccak_values,
                false,
                std::move(state_root),
                aux,
                sm);

            if (erase && (iter & 1) != 0) {
                fprintf(stdout, "> erase iter = %lu\n", iter);
                fflush(stdout);
                state_root = batch_upsert_commit(
                    csv_writer,
                    block_no++,
                    (iter % 100) * SLICE_LEN, /* vec_idx */
                    key_offset, /* key offset */
                    SLICE_LEN, /* nkeys */
                    keccak_keys,
                    keccak_values,
                    true,
                    std::move(state_root),
                    aux,
                    sm);

                fprintf(stdout, "> dup batch iter = %lu\n", iter);

                state_root = batch_upsert_commit(
                    csv_writer,
                    block_no++,
                    (iter % 100) * SLICE_LEN, /* vec_idx */
                    key_offset, /* key offset */
                    SLICE_LEN, /* nkeys */
                    keccak_keys,
                    keccak_values,
                    false,
                    std::move(state_root),
                    aux,
                    sm);
            }
        }

        auto end_test = std::chrono::steady_clock::now();
        auto test_secs =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    end_test - begin_test)
                    .count()) /
            1000000.0;
        printf("\nTotal test time: %f secs.\n", test_secs);
        if (csv_writer) {
            csv_writer << "\n\"Total test time:\"," << test_secs << std::endl;
        }
    }
    catch (const CLI::CallForHelp &e) {
        std::cout << cli.help() << std::flush;
    }
    catch (std::exception const &e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
