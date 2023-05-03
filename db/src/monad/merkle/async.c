#include <monad/merkle/merge.h>
#include <monad/trie/io.h>

void async_write_request(unsigned char *const buffer, unsigned long long offset)
{
    // while (inflight >= URING_ENTRIES) {
    //     poll_uring();
    //     printf(
    //         "buffer 0x%lx, write_buffer 0x%lx\n",
    //         (int64_t)buffer,
    //         (int64_t)write_buffer);
    // }

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

    io_uring_prep_write(sqe, fd, buffer, WRITE_BUFFER_SIZE, offset);
    write_uring_data_t *uring_data = (write_uring_data_t *)cpool_ptr31(
        &tmp_pool, cpool_reserve31(&tmp_pool, sizeof(write_uring_data_t)));
    cpool_advance31(&tmp_pool, sizeof(write_uring_data_t));
    *uring_data = (write_uring_data_t){.rw_flag = IS_WRITE, .buffer = buffer};
    io_uring_sqe_set_data(sqe, uring_data);
    io_uring_submit(ring);
    ++inflight;
}

void async_read_request(merge_uring_data_t *const uring_data)
{
    // get io_uring sqe, if no available entry, wait on poll() to reap some
    while (inflight >= URING_ENTRIES) {
        poll_uring();
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

    // prep uring data
    int64_t offset =
        uring_data->prev_parent->children[uring_data->prev_child_i].fnext;
    // get_read_buffer, buffer_off, and read_size
    int64_t off_aligned = (offset >> 9) << 9;
    size_t buffer_off = offset - off_aligned;
    size_t read_size = READ_BUFFER_SIZE;
    if (READ_BUFFER_SIZE - buffer_off < MAX_DISK_NODE_SIZE) {
        // the node is across two read buffers
        read_size *= 2;
    }
    unsigned char *rd_buffer =
        (unsigned char *)aligned_alloc(ALIGNMENT, read_size);

    uring_data->buffer = rd_buffer;
    uring_data->buffer_off = buffer_off;
    assert(uring_data->buffer);

    // prep for read
    io_uring_prep_read(sqe, fd, rd_buffer, read_size, off_aligned);
    // submit an io_uring request, with merge_params data
    io_uring_sqe_set_data(sqe, uring_data);
    io_uring_submit(ring);
    ++inflight;
    ++inflight_rd;
    ++n_rd_per_block;
}

void poll_uring()
{
    struct io_uring_cqe *cqe;
    // get one completed request
    int ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "io_uring_wait_cqe fail: %s\n", strerror(-ret));
        exit(1);
    }
    if (cqe->res < 0) {
        /* The system call invoked asynchonously failed */
        fprintf(stderr, "async syscall failed: %s\n", strerror(-cqe->res));
        // TODO: resubmit the request
        exit(1);
    }
    --inflight;

    // get data from io_uring
    void *uring_data = io_uring_cqe_get_data(cqe);
    if (!uring_data) {
        fprintf(stderr, "Error getting user data from CQE\n");
        exit(1);
    }
    io_uring_cqe_seen(ring, cqe);

    // handle write
    if (((write_uring_data_t *)uring_data)->rw_flag == IS_WRITE) {
        assert(cqe->res == WRITE_BUFFER_SIZE);
        free(((write_uring_data_t *)uring_data)->buffer);
        return;
    }
    // handle read
    --inflight_rd;
    merge_uring_data_t *data = (merge_uring_data_t *)uring_data;
    // construct the node from the read buffer
    merkle_node_t *node =
        deserialize_node_from_buffer(data->buffer + data->buffer_off);
    assert(node->nsubnodes);
    assert(node->mask);

    data->prev_parent->children[data->prev_child_i].next = node;
    free(data->buffer);

    // callback to merge_trie() from where that request left out
    merge_trie(
        data->prev_parent,
        data->prev_child_i,
        data->tmp_parent,
        data->tmp_branch_i,
        data->pi,
        data->new_parent,
        data->new_branch_arr_i,
        data->parent);
    upward_update_data(data->parent);
}
