#include <monad/mpt/db_error.hpp>
#include <monad/mpt/read_only_db.hpp>

MONAD_MPT_NAMESPACE_BEGIN

ReadOnlyDb::ReadOnlyDb(ReadOnlyOnDiskDbConfig const &options)
    : pool_{[&] -> async::storage_pool {
        async::storage_pool::creation_flags pool_options;
        pool_options.open_read_only = true;
        pool_options.disable_mismatching_storage_pool_check =
            options.disable_mismatching_storage_pool_check;
        MONAD_ASSERT(!options.dbname_paths.empty());
        return async::storage_pool{
            options.dbname_paths,
            async::storage_pool::mode::open_existing,
            pool_options};
    }()}
    , ring_{io::Ring{options.uring_entries, options.sq_thread_cpu}}
    , rwbuf_{io::make_buffers_for_read_only(
          ring_, options.rd_buffers,
          async::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE)}
    , io_{pool_, rwbuf_}
    , aux_{&io_}
    , last_loaded_offset_{aux_.get_root_offset()}
    , root_{Node::UniquePtr{read_node_blocking(pool_, last_loaded_offset_)}}
{
}

bool ReadOnlyDb::is_latest() const
{
    return last_loaded_offset_ == aux_.get_root_offset();
}

void ReadOnlyDb::load_latest()
{
    last_loaded_offset_ = aux_.get_root_offset();
    root_.reset(read_node_blocking(pool_, last_loaded_offset_));
}

Result<NodeCursor> ReadOnlyDb::get(NodeCursor root, NibblesView const key) const
{
    auto const [it, result] = find_blocking(aux_, root, key);
    if (result != find_result::success) {
        return system_error2::errc::no_such_file_or_directory;
    }
    MONAD_DEBUG_ASSERT(it.node != nullptr);
    MONAD_DEBUG_ASSERT(it.node->has_value());
    return it;
}

Result<byte_string_view>
ReadOnlyDb::get(NibblesView key, uint64_t block_id) const
{
    auto res = get(root(), serialize_as_big_endian<BLOCK_NUM_BYTES>(block_id));
    if (!res.has_value()) {
        return DbError::key_not_found;
    }
    res = get(res.value(), key);
    if (!res.has_value()) {
        return DbError::key_not_found;
    }
    return res.value().node->value();
}

Result<byte_string_view>
ReadOnlyDb::get_data(NodeCursor root, NibblesView const key) const
{
    auto res = get(root, key);
    if (!res.has_value()) {
        return DbError::key_not_found;
    }
    MONAD_DEBUG_ASSERT(res.value().node != nullptr);

    return res.value().node->data();
}

Result<byte_string_view>
ReadOnlyDb::get_data(NibblesView const key, uint64_t const block_id) const
{
    auto res = get(root(), serialize_as_big_endian<BLOCK_NUM_BYTES>(block_id));
    if (!res.has_value()) {
        return DbError::key_not_found;
    }
    return get_data(res.value(), key);
}

MONAD_MPT_NAMESPACE_END
