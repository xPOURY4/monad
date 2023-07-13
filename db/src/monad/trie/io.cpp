#include <monad/trie/io.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

AsyncIO::async_write_node_result
AsyncIO::async_write_node(merkle_node_t const *const node)
{
    // always one write buffer in use but not yet submitted
    while (records_.inflight >= uring_.get_sq_entries() - 1) {
        poll_uring();
    }

    unsigned size = get_disk_node_size(node);
    if (size + buffer_idx_ > rwbuf_.get_write_size()) {
        // submit write request
        submit_write_request(
            write_buffer_, block_off_, rwbuf_.get_write_size());

        // renew buffer
        block_off_ += rwbuf_.get_write_size();
        write_buffer_ = wr_pool_.alloc();
        MONAD_ASSERT(write_buffer_);
        buffer_idx_ = 0;
    }
    file_offset_t ret = block_off_ + buffer_idx_;
    serialize_node_to_buffer(write_buffer_ + buffer_idx_, node, size);
    buffer_idx_ += size;
    return {ret, size};
}

void AsyncIO::submit_request(
    std::span<std::byte> buffer, file_offset_t offset, void *uring_data,
    bool is_write)
{
    // Trap unintentional use of high bit offsets
    MONAD_ASSERT(offset <= (file_offset_t(1) << 48));

    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    if (is_write) {
        io_uring_prep_write_fixed(sqe, WRITE, buffer.data(), buffer.size(), offset, 1);
    }
    else {
        io_uring_prep_read_fixed(sqe, READ, buffer.data(), buffer.size(), offset, 0);
    }
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}

void AsyncIO::submit_write_request(
    unsigned char *const buffer, file_offset_t const offset,
    unsigned const write_size)
{
    // write user data
    write_uring_data_t::unique_ptr_type user_data =
        write_uring_data_t::make(*this, buffer);

    // We release the ownership of uring_data to io_uring. We reclaim
    // ownership after we reap the i/o completion.
    submit_request(
        {(std::byte *)buffer, write_size},
        offset,
        user_data.release(),
        true);
    ++records_.inflight;
}

void AsyncIO::poll_uring()
{
    // TODO: handle resource temporarily unavailable error, resubmit the
    // request
    struct io_uring_cqe *cqe;

    MONAD_ASSERT(
        !io_uring_wait_cqe(const_cast<io_uring *>(&uring_.get_ring()), &cqe));

    void *data = io_uring_cqe_get_data(cqe);
    MONAD_ASSERT(data);
    auto *state = reinterpret_cast<erased_connected_operation *>(data);
    result<size_t> res = (cqe->res < 0) ? result<size_t>(posix_code(-cqe->res))
                                        : result<size_t>(cqe->res);
    io_uring_cqe_seen(const_cast<io_uring *>(&uring_.get_ring()), cqe);
    cqe = nullptr;

    --records_.inflight;
    if (state->is_append()) {
        MONAD_ASSERT(res);
        auto write_data = static_cast<write_uring_data_t *>(state);
        wr_pool_.release(write_data->buffer);
        // Reclaim ownership, and release
        (void)write_uring_data_t::unique_ptr_type(
            reinterpret_cast<write_uring_data_t *>(data));
    }
    else {
        state->completed(std::move(res));
    }
}

MONAD_TRIE_NAMESPACE_END