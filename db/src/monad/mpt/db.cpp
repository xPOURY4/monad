#include <monad/async/config.hpp>
#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/async/io.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/result.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/db.hpp>
#include <monad/mpt/db_options.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/traverse.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/update.hpp>

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <unistd.h>
#include <utility>

// TODO unstable paths between versions
#if __has_include(<boost/outcome/experimental/status-code/generic_code.hpp>)
    #include <boost/outcome/experimental/status-code/generic_code.hpp>

#else
    #include <boost/outcome/experimental/status-code/status-code/generic_code.hpp>
#endif

#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

Db::OnDisk::OnDisk(DbOptions const &options)
    : pool{[&] -> async::storage_pool {
        MONAD_ASSERT(options.on_disk);
        if (options.dbname_paths.empty()) {
            return async::storage_pool{async::use_anonymous_inode_tag{}};
        }
        // initialize db file on disk
        for (auto const &dbname_path : options.dbname_paths) {
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
                        1ULL * 1024 * 1024 * 1024 * 1024 + 24576 /* 1Tb */)) {
                    throw std::system_error(errno, std::system_category());
                }
            }
        }
        return async::storage_pool{
            options.dbname_paths,
            options.append ? async::storage_pool::mode::open_existing
                           : async::storage_pool::mode::truncate};
    }()}
    , ring{io::Ring{options.uring_entries, options.sq_thread_cpu}}
    , rwbuf{ring, options.rd_buffers, options.wr_buffers, async::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE, async::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE}
    , io{pool, ring, rwbuf}
{
}

Db::Db(StateMachine &machine, DbOptions const &options)
    : on_disk_{options.on_disk ? std::make_optional<OnDisk>(options) : std::nullopt}
    , aux_{options.on_disk ? &on_disk_.value().io : nullptr}
    , root_(
          (options.on_disk && options.append)
              ? Node::UniquePtr{read_node_blocking(
                    on_disk_.value().pool, aux_.get_root_offset())}
              : Node::UniquePtr{})
    , machine_{machine}
{
    MONAD_DEBUG_ASSERT(aux_.is_in_memory() || on_disk_.has_value());
}

Result<byte_string_view> Db::get(NibblesView const key)
{
    auto const [node, result] = find_blocking(aux_, root_.get(), key);
    if (result != find_result::success) {
        return system_error2::errc::no_such_file_or_directory;
    }
    MONAD_DEBUG_ASSERT(node != nullptr);

    if (!node->has_value()) {
        return system_error2::errc::no_such_file_or_directory;
    }

    return node->value();
}

Result<byte_string_view> Db::get_data(NibblesView const key)
{
    auto const [node, result] = find_blocking(aux_, root_.get(), key);
    if (result != find_result::success) {
        return system_error2::errc::no_such_file_or_directory;
    }
    MONAD_DEBUG_ASSERT(node != nullptr);

    return node->data();
}

void Db::upsert(UpdateList list)
{
    root_ = mpt::upsert(aux_, machine_, std::move(root_), std::move(list));
}

void Db::traverse(NibblesView const root, TraverseMachine &machine)
{
    auto const [node, result] = find_blocking(aux_, root_.get(), root);
    if (result != find_result::success) {
        return;
    }
    MONAD_DEBUG_ASSERT(node != nullptr);
    preorder_traverse(aux_, *node, machine);
}

MONAD_MPT_NAMESPACE_END
