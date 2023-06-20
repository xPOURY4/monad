#include <monad/trie/io.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

int64_t AsyncIO::async_write_node(merkle_node_t *node)
{
    // always one write buffer in use but not submitted
    while (records_.inflight >= uring_.get_sq_entries() - 1) {
        poll_uring();
    }

    size_t size = get_disk_node_size(node);
    if (size + buffer_idx_ > rwbuf_.get_write_size()) {
        // submit write request
        submit_write_request(write_buffer_, block_off_);

        // renew buffer
        block_off_ += rwbuf_.get_write_size();
        write_buffer_ = wr_pool_.alloc();
        MONAD_ASSERT(write_buffer_);
        buffer_idx_ = 0;
    }
    int64_t ret = block_off_ + buffer_idx_;
    serialize_node_to_buffer(write_buffer_ + buffer_idx_, node);
    buffer_idx_ += size;
    return ret;
}

void AsyncIO::submit_request(
    unsigned char *const buffer, unsigned int nbytes, unsigned long long offset,
    void *const uring_data, bool is_write)
{
    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    if (is_write) {
        io_uring_prep_write_fixed(sqe, 0, buffer, nbytes, offset, 1);
    }
    else {
        io_uring_prep_read_fixed(sqe, 0, buffer, nbytes, offset, 0);
    }
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}

void AsyncIO::submit_write_request(unsigned char *buffer, int64_t const offset)
{
    // write user data
    write_uring_data_t *user_data = write_uring_data_t::pool.malloc();
    *user_data = (write_uring_data_t){
        .rw_flag = uring_data_type_t::IS_WRITE, .buffer = buffer};

    submit_request(buffer, rwbuf_.get_write_size(), offset, user_data, true);
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
    io_uring_cqe_seen(const_cast<io_uring *>(&uring_.get_ring()), cqe);

    --records_.inflight;
    if (auto write_data = reinterpret_cast<write_uring_data_t *>(data);
        write_data->rw_flag == uring_data_type_t::IS_WRITE) {
        wr_pool_.release(reinterpret_cast<write_uring_data_t *>(data)->buffer);
        write_uring_data_t::pool.destroy(
            reinterpret_cast<write_uring_data_t *>(data));
    }
    else {
        readcb_(data);
    }
}

MONAD_TRIE_NAMESPACE_END