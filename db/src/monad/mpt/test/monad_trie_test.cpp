#include "test_fixtures_base.hpp"

#include <monad/async/config.hpp>
#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/async/io.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/keccak.h>
#include <monad/core/small_prng.hpp>
#include <monad/core/unaligned.hpp>
#include <monad/core/unordered_map.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/state_machine.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/util.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h> // for SYS_gettid
#include <unistd.h> // for syscall()

#define SLICE_LEN 100000
#define MAX_NUM_KEYS 250000000

using namespace monad::mpt;

static void ctrl_c_handler(int s)
{
    (void)s;
    exit(0);
}

void __print_bytes_in_hex(monad::byte_string_view arr)
{
    fprintf(stdout, "0x");
    for (auto const &c : arr) {
        fprintf(stdout, "%02x", (uint8_t)c);
    }
    fprintf(stdout, "\n");
}

/*  Commit one batch of updates
    key_offset: insert key starting from this number
    nkeys: number of keys to insert in this batch
*/
Node::UniquePtr batch_upsert_commit(
    std::ostream &csv_writer, uint64_t block_id, uint64_t const vec_idx,
    uint64_t const key_offset, uint64_t const nkeys,
    std::vector<monad::byte_string> &keccak_keys,
    std::vector<monad::byte_string> &keccak_values, bool const erase,
    bool compaction, Node::UniquePtr prev_root, UpdateAux &aux,
    StateMachine &sm)
{
    std::vector<Update> update_alloc;
    update_alloc.reserve(SLICE_LEN);
    UpdateList state_updates;
    for (uint64_t i = 0; i < nkeys; ++i) {
        state_updates.push_front(update_alloc.emplace_back(
            erase ? make_erase(keccak_keys[i + vec_idx])
                  : make_update(
                        keccak_keys[i + vec_idx], keccak_values[i + vec_idx])));
    }

    auto ts_before = std::chrono::steady_clock::now();
    auto new_root = aux.upsert_with_fixed_history_len(
        std::move(prev_root),
        sm,
        std::move(state_updates),
        block_id,
        compaction);
    auto ts_after = std::chrono::steady_clock::now();
    double tm_ram = static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            ts_after - ts_before)
                            .count()) /
                    1000000000.0;

    auto block_num = serialize_as_big_endian<6>(block_id);
    auto [state_root, res] = find_blocking(aux, new_root.get(), block_num);
    fprintf(stdout, "root->data : ");
    __print_bytes_in_hex(state_root->data());

    auto const latest_fast_chunk = aux.node_writer_fast->sender().offset();
    auto const latest_slow_chunk = aux.node_writer_slow->sender().offset();
    fprintf(
        stdout,
        "next_key_id: %lu, nkeys upserted: %lu, upsert+commit in RAM: %f /s, "
        "total_t %.4f s\nlatest fast chunk written to: idx %u, count %u\n"
        "latest slow chunk written to: idx %u, count %u\n=====\n",
        (key_offset + vec_idx + nkeys) % MAX_NUM_KEYS,
        nkeys,
        (double)nkeys / tm_ram,
        tm_ram,
        latest_fast_chunk.id,
        (uint32_t)aux.db_metadata()
            ->at(latest_fast_chunk.id)
            ->insertion_count(),
        latest_slow_chunk.id,
        (uint32_t)aux.db_metadata()
            ->at(latest_slow_chunk.id)
            ->insertion_count());
    fflush(stdout);
    if (csv_writer) {
        csv_writer << (key_offset + vec_idx + nkeys) << ","
                   << ((double)nkeys / tm_ram) << std::endl;
    }

    return new_root;
}

void prepare_keccak(
    size_t nkeys, std::vector<monad::byte_string> &keccak_keys,
    std::vector<monad::byte_string> &keccak_values, size_t key_offset,
    bool realistic_corpus, bool is_random = false)
{
    size_t val;
    // prepare keccak
    if (realistic_corpus) {
        /* We know from analysing Ethereum chain history that:

        - Under 2% of all accounts are recipients of 65% of all transactions.
        - Under 5% of all accounts are recipients of 75% of all transactions.
        - Around one third of all accounts are recipients of 90% of all
        transactions.
        - Around two thirds of all accounts are recipients of 95% of all
        transactions.

        Or put another way:
        - One third of all accounts are recipients of 5% of all transactions.
        - Two thirds of all accounts are recipients of 10% of all transactions.
        - 95% of all accounts are recipients of 25% of all transactions.
        - 98% of all accounts are recipients of 35% of all transactions.
        - So just under 2% of all accounts are recipients of two thirds of all
        transactions.
        */
        static constexpr auto MULTIPLIER = 3.7;
        static auto const DIVISOR = pow(MULTIPLIER, MULTIPLIER);
        static constexpr uint32_t TOTAL_KEYS = 500000000;
        static double const MAX_RAND = double(monad::small_prng::max());
        monad::small_prng rand(uint32_t(key_offset / (100 * SLICE_LEN)));
        monad::unordered_flat_set<uint32_t> seen;
        for (size_t i = 0; i < nkeys; ++i) {
            if ((i % SLICE_LEN) == 0) {
                seen.clear();
            }
            for (;;) {
                double r = double(rand()) / MAX_RAND;
                r = pow(MULTIPLIER, MULTIPLIER * r) / DIVISOR;
                uint32_t j = uint32_t(double(TOTAL_KEYS) * r);
                if (!seen.contains(j)) {
                    seen.insert(j);
                    keccak_keys[i].resize(32);
                    keccak256(
                        (unsigned char const *)&j, 4, keccak_keys[i].data());

                    val = (i + key_offset) * 2;
                    keccak_values[i].resize(32);
                    keccak256(
                        (unsigned char const *)&val,
                        8,
                        keccak_values[i].data());
                    break;
                }
            }
        }
    }
    else {
        std::random_device rd; // a seed source for the random number engine
        std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
        std::uniform_int_distribution<> distrib(0, MAX_NUM_KEYS - 1);
        MONAD_ASSERT(distrib.min() == 0 && distrib.max() == MAX_NUM_KEYS - 1);

        size_t key;
        monad::unordered_flat_set<size_t> keys_per_slice; // dedup
        // prepare keccak
        for (size_t i = 0; i < nkeys; ++i) {
            if (i % SLICE_LEN == 0) {
                keys_per_slice.clear();
            }
            if (is_random) {
                do {
                    key = (size_t)distrib(gen);
                }
                while (keys_per_slice.find(key) != keys_per_slice.end());
                keys_per_slice.insert(key);
            }
            else {
                key = i + key_offset;
            }
            keccak_keys[i].resize(32);
            keccak256((unsigned char const *)&key, 8, keccak_keys[i].data());
            val = key * 2;
            keccak_values[i].resize(32);
            keccak256((unsigned char const *)&val, 8, keccak_values[i].data());
        }
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
    std::vector<std::filesystem::path> dbname_paths;
    std::filesystem::path csv_stats_path;
    uint64_t key_offset = 0;
    unsigned sq_thread_cpu = 15;
    bool erase = false;
    bool in_memory = false; // default is on disk
    bool empty_cpu_caches = false;
    bool realistic_corpus = false;
    bool random_keys = false;
    bool compaction = false;
    int file_size_db = 512; // truncate to 512 gb by default

    CLI::App cli{"monad_merge_trie_test"};
    try {
        printf("main() runs on tid %ld\n", syscall(SYS_gettid));
        cli.add_flag("--append", append, "append at a specific block in db");
        cli.add_option(
            "--db-names",
            dbname_paths,
            "db file names, can have more than one");
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
        cli.add_flag(
            "--realistic-corpus",
            realistic_corpus,
            "use test corpus resembling historical patterns");
        cli.add_flag(
            "--random-keys", random_keys, "generate random integers as keys");
        cli.add_flag("--compaction", compaction, "perform compaction on disk");
        cli.add_option(
            "--file-size-gb",
            file_size_db,
            "size to create file to if not already exist, only apply to file "
            "not blkdev");
        cli.parse(argc, argv);

        MONAD_ASSERT(in_memory + append < 2);
        if (in_memory && compaction) {
            throw std::invalid_argument(
                "Invalid to init with compaction on in-memory triedb.");
        }
        if (realistic_corpus) {
            random_keys = false;
        }

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
                            nullptr,
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

        if (dbname_paths.empty()) {
            dbname_paths.emplace_back("test.db");
        }
        for (auto const &dbname_path : dbname_paths) {
            if (!std::filesystem::exists(dbname_path)) {
                int const fd = ::open(
                    dbname_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
                if (-1 == fd) {
                    throw std::system_error(errno, std::system_category());
                }
                auto unfd =
                    monad::make_scope_exit([fd]() noexcept { ::close(fd); });
                if (-1 ==
                    ::ftruncate(
                        fd,
                        (int64_t)file_size_db * 1024 * 1024 * 1024 + 24576)) {
                    throw std::system_error(errno, std::system_category());
                }
            }
        }
        MONAD_ASYNC_NAMESPACE::storage_pool pool{
            {dbname_paths},
            append ? MONAD_ASYNC_NAMESPACE::storage_pool::mode::open_existing
                   : MONAD_ASYNC_NAMESPACE::storage_pool::mode::truncate};

        // init uring
        monad::io::Ring ring(128, sq_thread_cpu);

        // init buffer
        monad::io::Buffers rwbuf{
            ring,
            8192 * 16,
            16,
            MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
            MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};

        auto io = MONAD_ASYNC_NAMESPACE::AsyncIO{pool, ring, rwbuf};

        UpdateAux aux{};
        monad::test::StateMachineWithBlockNo sm{};
        if (!in_memory) {
            aux.set_io(&io);
        }

        Node::UniquePtr root{};
        if (append) {
            root.reset(
                read_node_blocking(io.storage_pool(), aux.get_root_offset()));
            aux.restore_state_history_disk_infos(*root);
        }
        uint64_t block_no = aux.next_block_id();

        auto begin_test = std::chrono::steady_clock::now();
        uint64_t const max_key = n_slices * SLICE_LEN + key_offset;
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
                    key_offset,
                    realistic_corpus,
                    random_keys);
                fprintf(
                    stdout, "Finish preparing keccak.\nStart transactions\n");
                fflush(stdout);
                auto end_prepare_keccak = std::chrono::steady_clock::now();
                begin_test += end_prepare_keccak - begin_prepare_keccak;
            }

            cpu_cache_emptier();
            root = batch_upsert_commit(
                csv_writer,
                block_no++,
                (iter % 100) * SLICE_LEN, /* vec_idx */
                key_offset, /* key offset */
                SLICE_LEN, /* nkeys */
                keccak_keys,
                keccak_values,
                false,
                compaction,
                std::move(root),
                aux,
                sm);

            if (erase && (iter & 1) != 0) {
                fprintf(stdout, "> erase iter = %lu\n", iter);
                fflush(stdout);
                root = batch_upsert_commit(
                    csv_writer,
                    block_no++,
                    (iter % 100) * SLICE_LEN, /* vec_idx */
                    key_offset, /* key offset */
                    SLICE_LEN, /* nkeys */
                    keccak_keys,
                    keccak_values,
                    true,
                    compaction,
                    std::move(root),
                    aux,
                    sm);

                fprintf(stdout, "> dup batch iter = %lu\n", iter);

                root = batch_upsert_commit(
                    csv_writer,
                    block_no++,
                    (iter % 100) * SLICE_LEN, /* vec_idx */
                    key_offset, /* key offset */
                    SLICE_LEN, /* nkeys */
                    keccak_keys,
                    keccak_values,
                    false,
                    compaction,
                    std::move(root),
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
        uint64_t bytes_used = 0;
        for (auto const &device : pool.devices()) {
            bytes_used += device.capacity().second;
        }
        printf(
            "\nTotal test time: %f secs. Total storage consumed: %f Gb\n",
            test_secs,
            double(bytes_used) / 1024.0 / 1024.0 / 1024.0);
        if (csv_writer) {
            csv_writer << "\n\"Total test time:\"," << test_secs;
            csv_writer << "\n\"Total storage consumed:\"," << bytes_used
                       << std::endl;
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
