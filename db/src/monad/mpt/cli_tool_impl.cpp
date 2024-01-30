#include <CLI/CLI.hpp>

#include "cli_tool_impl.hpp"

#include <monad/async/config.hpp>
#include <monad/async/io.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/detail/db_metadata.hpp>

#include <monad/async/storage_pool.hpp>
#include <monad/mpt/detail/kbhit.hpp>
#include <monad/mpt/trie.hpp>

#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

std::string print_bytes(MONAD_ASYNC_NAMESPACE::file_offset_t bytes_)
{
    auto bytes = double(bytes_);
    std::stringstream s;
    if (bytes >= 0.9 * 1024 * 1024 * 1024 * 1024 * 1024) {
        bytes /= 1024.0 * 1024 * 1024 * 1024 * 1024;
        s << std::setprecision(int(bytes / 10) + 2) << bytes << " Pb";
        return std::move(s).str();
    }
    if (bytes >= 0.9 * 1024 * 1024 * 1024 * 1024) {
        bytes /= 1024.0 * 1024 * 1024 * 1024;
        s << std::setprecision(int(bytes / 10) + 2) << bytes << " Tb";
        return std::move(s).str();
    }
    if (bytes >= 0.9 * 1024 * 1024 * 1024) {
        bytes /= 1024.0 * 1024 * 1024;
        s << std::setprecision(int(bytes / 10) + 2) << bytes << " Gb";
        return std::move(s).str();
    }
    if (bytes >= 0.9 * 1024 * 1024) {
        bytes /= 1024.0 * 1024;
        s << std::setprecision(int(bytes / 10) + 2) << bytes << " Mb";
        return std::move(s).str();
    }
    if (bytes >= 0.9 * 1024) {
        bytes /= 1024.0;
        s << std::setprecision(int(bytes / 10) + 2) << bytes << " Kb";
        return std::move(s).str();
    }
    s << bytes << " bytes";
    return std::move(s).str();
}

int main_impl(
    std::ostream &cout, std::ostream &cerr, std::span<std::string_view> args)
{
    CLI::App cli("Tool for managing MPT databases", "monad_mpt");
    cli.footer(R"(Suitable sources of block storage:

1. Raw partitions on a storage device.
2. The storage device itself.
3. A file on a filing system (use 'truncate -s 1T sparsefile' to create and
set it to the desired size beforehand).

The storage source order must be identical to database creation, as must be
the source type, size and device id, otherwise the database cannot be
opened.
)");
    try {
        MONAD_ASYNC_NAMESPACE::storage_pool::creation_flags flags;
        uint8_t chunk_capacity = flags.chunk_capacity;
        bool open_writable = false;
        bool no_prompt = false;
        bool create_database = false;
        bool truncate_database = false;
        std::vector<std::filesystem::path> storage_paths;
        cli.add_option(
               "--storage",
               storage_paths,
               "one or more sources of block storage")
            ->required();
        cli.add_flag(
            "--writable",
            open_writable,
            "open the database for modification, without it is opened "
            "read-only. Opening for modification will enable metadata healing "
            "if database was closed uncleanly.");
        cli.add_flag(
            "--yes", no_prompt, "do not prompt before doing dangerous things");
        cli.add_flag(
            "--create",
            create_database,
            "create a new database if needed, otherwise opens existing "
            "(implies --writable)");
        cli.add_flag(
            "--truncate",
            truncate_database,
            "truncates an existing database to empty, efficiently discarding "
            "all existing storage (implies --writable)");
        cli.add_option(
            "--chunk-capacity",
            chunk_capacity,
            "set chunk capacity during database creation (default is 28, 1<<28 "
            "= 256Mb, max is 31)");
        {
            // Weirdly CLI11 wants reversed args for its vector consuming
            // overload
            std::vector<std::string> rargs(args.rbegin(), --args.rend());
            cli.parse(std::move(rargs));
        }

        auto mode = MONAD_ASYNC_NAMESPACE::storage_pool::mode::open_existing;
        flags.chunk_capacity = chunk_capacity & 31;
        flags.open_read_only = !open_writable;
        if (create_database) {
            mode = MONAD_ASYNC_NAMESPACE::storage_pool::mode::create_if_needed;
            flags.open_read_only = false;
        }
        else if (truncate_database) {
            mode = MONAD_ASYNC_NAMESPACE::storage_pool::mode::truncate;
            flags.open_read_only = false;
            if (!no_prompt) {
                auto answer =
                    tty_ask_question("WARNING: --truncate will destroy all "
                                     "existing data. Are you sure?\n");
                cout << std::endl;
                if (tolower(answer) != 'y') {
                    cout << "Aborting." << std::endl;
                    return 0;
                }
            }
        }
        MONAD_ASYNC_NAMESPACE::storage_pool pool{{storage_paths}, mode, flags};
        monad::io::Ring ring(1, 0);
        monad::io::Buffers rwbuf{
            ring,
            2,
            2,
            MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
            MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};
        auto io = MONAD_ASYNC_NAMESPACE::AsyncIO{pool, ring, rwbuf};
        MONAD_MPT_NAMESPACE::UpdateAux<> aux{};
        aux.set_io(&io);

        cout << R"(MPT database on storages:
     Capacity      Used      %  Path)";
        auto const default_width = int(cout.width());
        auto const default_prec = int(cout.precision());
        std::fixed(cout);
        for (auto &device : pool.devices()) {
            auto cap = device.capacity();
            cout << "\n   " << std::setw(10) << print_bytes(cap.first)
                 << std::setw(10) << print_bytes(cap.second) << std::setw(6)
                 << std::setprecision(2)
                 << (100.0 * double(cap.second) / double(cap.first)) << "%  "
                 << device.current_path();
        }
        std::defaultfloat(cout);
        cout << std::setw(default_width) << std::setprecision(default_prec)
             << std::endl;

        cout << "MPT database internal lists:\n";
        auto print_list_info =
            [&](MONAD_MPT_NAMESPACE::detail::db_metadata::chunk_info_t const
                    *item,
                char const *name) {
                MONAD_ASYNC_NAMESPACE::file_offset_t total_capacity = 0,
                                                     total_used = 0;
                const auto begin_insertion_count = item->insertion_count();
                auto last_insertion_count = begin_insertion_count;
                do {
                    auto chunkid = item->index(aux.db_metadata());
                    last_insertion_count = item->insertion_count();
                    auto chunk = pool.activate_chunk(pool.seq, chunkid);
                    total_capacity += chunk->capacity();
                    total_used += chunk->size();
                    item = item->next(aux.db_metadata());
                }
                while (item != nullptr);
                cout << "     " << name << ": "
                     << (1 + uint32_t(last_insertion_count) -
                         uint32_t(begin_insertion_count))
                     << " chunks with capacity " << print_bytes(total_capacity)
                     << " used " << print_bytes(total_used) << std::endl;
            };
        print_list_info(aux.db_metadata()->fast_list_begin(), "Fast");
        print_list_info(aux.db_metadata()->slow_list_begin(), "Slow");
        print_list_info(aux.db_metadata()->free_list_begin(), "Free");
    }
    catch (const CLI::CallForHelp &e) {
        cout << cli.help() << std::flush;
    }
    catch (const CLI::RequiredError &e) {
        cerr << "FATAL: " << e.what() << "\n\n";
        cerr << cli.help() << std::flush;
        return 1;
    }
    catch (std::exception const &e) {
        cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
