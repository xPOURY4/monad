#include <monad/trie/io.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

void AsyncIO::submit_request(
    unsigned char *const buffer, unsigned int nbytes, unsigned long long offset,
    void *const uring_data, bool is_write)
{
    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_TRIE_ASSERT(sqe);

    if (is_write) {
        io_uring_prep_write(sqe, 0, buffer, nbytes, offset);
    }
    else {
        io_uring_prep_read(sqe, 0, buffer, nbytes, offset);
    }
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    io_uring_submit(const_cast<io_uring *>(&uring_.get_ring()));
}

[[nodiscard]] int64_t AsyncIO::async_write_node(merkle_node_t *node)
{
    size_t size = get_disk_node_size(node);
    while (size + buffer_idx_ > WRITE_BUFFER_SIZE) {
        unsigned char *prev_buffer = write_buffer_;
        int64_t prev_block_off = block_off_;
        block_off_ += WRITE_BUFFER_SIZE;
        write_buffer_ = get_avail_buffer(WRITE_BUFFER_SIZE);
        *write_buffer_ = BLOCK_TYPE_DATA;
        buffer_idx_ = 1;

        async_write_request(prev_buffer, prev_block_off);
    }
    int64_t ret = block_off_ + buffer_idx_;
    serialize_node_to_buffer(write_buffer_ + buffer_idx_, node);
    buffer_idx_ += size;
    return ret;
}

void AsyncIO::async_write_request(unsigned char *buffer, int64_t const offset)
{
    while (records_.inflight_ >= uring_.get_sq_entries()) {
        poll_uring();
    }
    // write user data
    // TODO: cleaner interface
    write_uring_data_t *user_data = (write_uring_data_t *)cpool_ptr29(
        cpool_, cpool_reserve29(cpool_, sizeof(write_uring_data_t)));
    cpool_advance29(cpool_, sizeof(write_uring_data_t));
    *user_data = (write_uring_data_t){
        .rw_flag = uring_data_type_t::IS_WRITE, .buffer = buffer};

    submit_request(buffer, WRITE_BUFFER_SIZE, offset, user_data, true);
    ++records_.inflight_;
}

// TODO
void AsyncIO::clearup()
{
    // while (records_.inflight_) {
    //     poll_uring();
    // }
    // if (write_buffer_) {
    //     if (buffer_idx_ > 1) {
    //         async_write_request(write_buffer_, block_off_);
    //         block_off_ += WRITE_BUFFER_SIZE;
    //         write_buffer_ = get_avail_buffer(WRITE_BUFFER_SIZE);
    //     }
    // }
}

MONAD_TRIE_NAMESPACE_END