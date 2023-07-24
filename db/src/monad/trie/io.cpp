#include <monad/trie/io.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

void AsyncIO::submit_request(
    std::span<std::byte> buffer, file_offset_t offset, void *uring_data)
{
    // Trap unintentional use of high bit offsets
    MONAD_ASSERT(offset <= (file_offset_t(1) << 48));
    assert((offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= READ_BUFFER_SIZE);
#ifndef NDEBUG
    memset(buffer.data(), 0xff, buffer.size());
#endif

    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    io_uring_prep_read_fixed(
        sqe, READ, buffer.data(), buffer.size(), offset, 0);
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}
void AsyncIO::submit_request(
    std::span<const std::byte> buffer, file_offset_t offset, void *uring_data)
{
    // Trap unintentional use of high bit offsets
    MONAD_ASSERT(offset <= (file_offset_t(1) << 48));
    assert((offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= WRITE_BUFFER_SIZE);

    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    io_uring_prep_write_fixed(
        sqe, WRITE, buffer.data(), buffer.size(), offset, 1);
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}

bool AsyncIO::poll_uring(bool blocking)
{
    // TODO: handle resource temporarily unavailable error, resubmit the
    // request
    struct io_uring_cqe *cqe = nullptr;

    if (blocking) {
        MONAD_ASSERT(!io_uring_wait_cqe(
            const_cast<io_uring *>(&uring_.get_ring()), &cqe));
    }
    else {
        if (0 != io_uring_peek_cqe(
                     const_cast<io_uring *>(&uring_.get_ring()), &cqe)) {
            return false;
        }
    }

    void *data = io_uring_cqe_get_data(cqe);
    MONAD_ASSERT(data);
    auto *state = reinterpret_cast<erased_connected_operation *>(data);
    result<size_t> res = (cqe->res < 0) ? result<size_t>(posix_code(-cqe->res))
                                        : result<size_t>(cqe->res);
    io_uring_cqe_seen(const_cast<io_uring *>(&uring_.get_ring()), cqe);
    cqe = nullptr;

    --records_.inflight;
    if (!state->is_write()) {
        --records_.inflight_rd;
    }
    state->completed(std::move(res));
    return true;
}

MONAD_TRIE_NAMESPACE_END