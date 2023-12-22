#include <monad/mpt/db.hpp>

#include <monad/async/config.hpp>
#include <monad/core/assert.h>
#include <monad/mpt/config.hpp>
#include <monad/mpt/db_options.hpp>
#include <monad/mpt/traverse.hpp>
#include <monad/mpt/trie.hpp>

// TODO unstable paths between versions
#if __has_include(<boost/outcome/experimental/status-code/generic_code.hpp>)
    #include <boost/outcome/experimental/status-code/generic_code.hpp>

#else
    #include <boost/outcome/experimental/status-code/status-code/generic_code.hpp>
#endif

#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

Db::OnDisk::OnDisk()
    : pool{async::use_anonymous_inode_tag{}}
    , ring{io::Ring{2, 0}}
    , rwbuf{ring, 2, 4, async::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE, async::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE}
    , io{pool, ring, rwbuf}
{
}

Db::Db(StateMachine &machine, DbOptions const &options)
    : on_disk_{options.on_disk ? std::make_optional<OnDisk>() : std::nullopt}
    , aux_{options.on_disk ? &on_disk_.value().io : nullptr}
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
