#include <malloc.h>
#include <monad/trie/io.h>
#include <stdint.h>

unsigned char *get_avail_buffer(size_t size)
{
    // TODO: get buffer from a buffer pool
    return (unsigned char *)aligned_alloc(ALIGNMENT, size);
}

int write_buffer_to_disk(int fd, unsigned char *buffer)
{
    unsigned char *write_pos = buffer;
    size_t bytes_write = 0;
    for (;;) {
        ssize_t write_res;
        write_res = write(fd, write_pos, WRITE_BUFFER_SIZE - bytes_write);
        if (write_res < 0) {
            printf("write fail fd %d, bytes_write = %lu.\n", fd, bytes_write);
            exit(errno);
        }
        bytes_write += write_res;
        if (bytes_write >= WRITE_BUFFER_SIZE) {
            break;
        }
        write_pos += write_res;
    }
    free(buffer);
    return 0;
}

unsigned
read_buffer_from_disk(int fd, int64_t const offset, unsigned char *const buffer)
{
    int64_t off_aligned = (offset >> 9) << 9;
    size_t read_start_pos = offset - off_aligned;
    size_t read_size = READ_BUFFER_SIZE;
    if (lseek(fd, off_aligned, SEEK_SET) != off_aligned) {
        perror("lseek to offset failed");
        exit(errno);
    }
    if (read(fd, buffer, read_size) == -1) {
        printf(
            "Fail to read buffer from db file %ld: %s",
            off_aligned,
            strerror(errno));
        exit(errno);
    }
    return read_start_pos;
}

// io uring
int init_uring(int fd, struct io_uring *ring, unsigned const kcpu)
{
    struct io_uring_params params;

    memset(&params, 0, sizeof(params));
    params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
    params.sq_thread_idle = SQ_THREAD_IDLE_MS;
    params.sq_thread_cpu = kcpu;

    int ret = io_uring_queue_init_params(URING_ENTRIES, ring, &params);
    if (ret) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
        return 1;
    }
    int fds[2];
    fds[0] = fd;
    ret = io_uring_register_files(ring, fds, 1);
    if (ret) {
        fprintf(stderr, "Error registering buffers: %s", strerror(-ret));
        return 1;
    }

    return 0;
}

void exit_uring(struct io_uring *ring)
{
    io_uring_queue_exit(ring);
    free(ring);
}