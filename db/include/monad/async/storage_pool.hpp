#pragma once

#include <monad/async/util.hpp>

#include <monad/async/detail/start_lifetime_as_polyfill.hpp>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <span>
#include <vector>

MONAD_ASYNC_NAMESPACE_BEGIN

/* \brief Makes available the lowest possible latency zoned storage, if `zonefs`
is available. Otherwise falls back to an emulation which can use a file on a
filesystem, or a block device.

\todo Actually implement `zonefs` support.

Linux `zonefs` when mounted exposes the NVMe zone namespaces as a POSIX
directory hierarchy. There are two directories in the root:

1. `cnv`, whose contents are all the conventional zones configured on the
storage device. Conventional zones can be read-write modified at will. These
have the block device emulation layer implemented by the storage device, and
thus reads from them have conventional SSD latencies e.g. 70 microseconds.

2. `seq`, whose contents are all the append-only zones configured on the storage
device. Sequential write zones can only be appended to for writes, and once
appended to, nothing already written can be modified. Read from these zones
bypass the block device emulation layer, and thus latencies are very
significantly improved e.g. 15-30 microseconds. Sequential write zones can be
recycled on request, all their contents are disposed of in a single operation
(this corresponds to NAND flash block erase), after which they can be
sequentially written into again.

You can read more about Linux `zonefs` at
https://docs.kernel.org/filesystems/zonefs.html.

This class is a thin wrapper around Linux `zonefs` if it is fed filesystem
paths to `zonefs` mounts. If it is fed a raw partition or a file on a
filesystem, it chops up that space into 256Mb chunks and exposes those as a
single conventional zone, and the remainder as sequential write zones. The
semantics are correctly emulated: resetting a chunk sends through a TRIM command
to the underlying storage, this will cause filesystems to truly deallocate
storage and raw partitions to issue a TRIM command to their hardware. This in
turn prevents garbage collection i/o storms caused by SSDs initiating forced
background TRIM during normal i/o to free up blocks, which introduces
pathological i/o performance loss at usually the most inconvenient times.
*/
class storage_pool
{
public:
    /*! \brief A source of backing storage for the storage pool.
     */
    class device
    {
        friend class storage_pool;

        int const _readfd, _writefd;
        const enum class _type_t : uint8_t {
            unknown,
            file,
            block_device,
            zoned_device
        } _type;
        const file_offset_t _size_of_file;
        struct metadata_t
        {
            // Preceding this is an array of uint32_t of chunk bytes used

            chunk_offset_t root_offset;
            uint32_t _spare0; // set aside for flags later
            uint32_t config_hash; // hash of this configuration
            uint32_t chunk_capacity;
            uint8_t magic[4]; // "MND0" for v1 of this metadata

            size_t chunks(file_offset_t end_of_this_offset) const noexcept
            {
                end_of_this_offset -= sizeof(metadata_t);
                auto ret =
                    end_of_this_offset / (chunk_capacity + sizeof(uint32_t));
                // We need the front CPU_PAGE_SIZE of this metadata to not
                // include any chunk
                auto endofchunks =
                    round_down_align<CPU_PAGE_BITS>(ret * chunk_capacity);
                auto startofmetadata = round_down_align<CPU_PAGE_BITS>(
                    end_of_this_offset - ret * sizeof(uint32_t));
                if (startofmetadata == endofchunks) {
                    return ret - 1;
                }
                return ret;
            }
            std::span<std::atomic<uint32_t>>
            chunk_bytes_used(file_offset_t end_of_this_offset) const noexcept
            {
                static_assert(
                    sizeof(uint32_t) == sizeof(std::atomic<uint32_t>));
                auto count = chunks(end_of_this_offset);
                return {
                    start_lifetime_as_array<std::atomic<uint32_t>>(
                        (std::byte *)this - count * sizeof(uint32_t), count),
                    count};
            }
            size_t total_size(file_offset_t end_of_this_offset) const noexcept
            {
                auto count = chunks(end_of_this_offset);
                return sizeof(metadata_t) + count * sizeof(uint32_t);
            }
        } *const _metadata;

        constexpr device(
            int readfd, int writefd, _type_t type, file_offset_t size_of_file,
            metadata_t *metadata)
            : _readfd(readfd)
            , _writefd(writefd)
            , _type(type)
            , _size_of_file(size_of_file)
            , _metadata(metadata)
        {
        }

    public:
        //! The current filesystem path of the device (it can change over time)
        std::filesystem::path current_path() const;
        //! Returns if this device is a file on a filesystem
        bool is_file() const noexcept
        {
            return _type == _type_t::file;
        }
        //! Returns if this device is a block device e.g. a raw partition
        bool is_block_device() const noexcept
        {
            return _type == _type_t::block_device;
        }
        //! Returns if this device is a zonefs mount
        bool is_zoned_device() const noexcept
        {
            return _type == _type_t::zoned_device;
        }
        //! Returns the number of chunks on this device
        size_t chunks() const;
        //! Returns the capacity of the device, and how much of that is
        //! currently filled with data, in that order.
        std::pair<file_offset_t, file_offset_t> capacity() const;
        //! Returns the latest root offset
        chunk_offset_t *root_offset() const
        {
            return &_metadata->root_offset;
        }
    };
    /*! \brief A zone chunk from storage, which is always managed by a shared
    ptr. When the shared ptr count reaches zero, any file descriptors or other
    resources associated with the chunk are released.
     */
    class chunk
    {
        friend class storage_pool;

    protected:
        class device &_device;
        int _read_fd{-1}, _write_fd{-1};
        const file_offset_t _offset{file_offset_t(-1)},
            _capacity{file_offset_t(-1)};
        uint32_t _chunkid{uint32_t(-1)};
        bool const _owns_readfd{false}, _owns_writefd{false},
            _append_only{false};

        constexpr chunk(
            class device &device, int read_fd, int write_fd,
            file_offset_t offset, file_offset_t capacity, uint32_t chunkid,
            bool owns_readfd, bool owns_writefd, bool append_only)
            : _device(device)
            , _read_fd(read_fd)
            , _write_fd(write_fd)
            , _offset(offset)
            , _capacity(capacity)
            , _chunkid(chunkid)
            , _owns_readfd(owns_readfd)
            , _owns_writefd(owns_writefd)
            , _append_only(append_only)
        {
        }

    public:
        chunk(chunk const &) = delete;
        chunk(chunk &&) = delete;
        virtual ~chunk();

        //! \brief Returns the storage device this chunk is stored upon
        class device &device() noexcept
        {
            return _device;
        }
        //! \brief Returns the storage device this chunk is stored upon
        const class device &device() const noexcept
        {
            return _device;
        }
        //! \brief Returns whether this chunk is a conventional write chunk
        bool is_conventional_write() const noexcept
        {
            return !_append_only;
        }
        //! \brief Returns whether this chunk is a sequential write chunk
        bool is_sequential_write() const noexcept
        {
            return _append_only;
        }
        //! \brief Returns a file descriptor able to read from the chunk, along
        //! with any offset which needs to be added to any i/o performed with it
        std::pair<int, file_offset_t> read_fd() const noexcept
        {
            return {_read_fd, _offset};
        }
        //! \brief Returns a file descriptor able to write to the chunk, along
        //! with any offset which needs to be added to any i/o performed with it
        std::pair<int, file_offset_t>
        write_fd(size_t bytes_which_shall_be_written) noexcept;
        //! \brief Returns the capacity of the zone
        file_offset_t capacity() const noexcept
        {
            return _capacity;
        }
        //! \brief Returns the chunk id of this zone on its device
        uint32_t device_zone_id() const noexcept
        {
            return _chunkid;
        }
        //! \brief Returns the current amount of the zone filled with data (note
        //! the OS syscall can sometimes lag reality for a few milliseconds)
        file_offset_t size() const;

        //! \brief Destroys the contents of the chunk, releasing the backing
        //! storage for use by others.
        void reset_size(uint32_t);

        //! \brief Destroys the contents of the chunk, releasing the backing
        //! storage for use by others.
        void destroy_contents();
    };
    /*! \brief A conventional zone chunk from the `cnv` subdirectory.
     */
    class cnv_chunk final : public chunk
    {
        friend class storage_pool;

        using chunk::chunk;

    public:
        bool is_conventional_write() const noexcept
        {
            return true;
        }
        bool is_sequential_write() const noexcept
        {
            return false;
        }
    };
    /*! \brief An append-only sequential write zone chunk from the `seq`
     * subdirectory.
     */
    class seq_chunk final : public chunk
    {
        friend class storage_pool;

        using chunk::chunk;

    public:
        bool is_conventional_write() const noexcept
        {
            return false;
        }
        bool is_sequential_write() const noexcept
        {
            return true;
        }
    };

    //! \brief What to do when opening the pool for use.
    enum class mode
    {
        open_existing,
        create_if_needed,
        truncate
    };

    using chunk_ptr = std::shared_ptr<class chunk>;
    using cnv_chunk_ptr = std::shared_ptr<cnv_chunk>;
    using seq_chunk_ptr = std::shared_ptr<seq_chunk>;

private:
    std::vector<device> _devices;

    // Lock protects everything below this
    mutable std::mutex _lock;
    struct _chunk_info
    {
        std::weak_ptr<class chunk> chunk;
        class device &device;
        uint32_t const zone_id;
    };
    std::vector<_chunk_info> _chunks[2];

    device _make_device(
        mode op, device::_type_t type, std::filesystem::path const &path,
        int fd, size_t chunk_capacity = 256ULL * 1024 * 1024);

    void _fill_chunks();

public:
    enum chunk_type
    {
        cnv = 0,
        seq = 1
    };
    //! \brief Constructs a storage pool from the list of backing storage
    //! sources
    storage_pool(
        std::span<std::filesystem::path> sources,
        mode mode = mode::create_if_needed);
    //! \brief Constructs a storage pool from a temporary anonymous inode.
    //! Useful for test code.
    storage_pool(
        use_anonymous_inode_tag, size_t chunk_capacity = 256ULL * 1024 * 1024);
    ~storage_pool();

    //! \brief Returns a list of the backing storage devices
    std::span<device const> devices() const noexcept
    {
        return {_devices};
    }
    //! \brief Returns the number of chunks for the specified type
    size_t chunks(chunk_type which) const noexcept
    {
        return _chunks[which].size();
    }
    //! \brief Returns the number of currently active chunks for the specified
    //! type
    size_t currently_active_chunks(chunk_type which) const noexcept;
    //! \brief Get an existing chunk, if it is activated
    std::shared_ptr<class chunk> chunk(chunk_type which, uint32_t id) const;
    //! \brief Activate a chunk (i.e. open file descriptors to it, if necessary)
    std::shared_ptr<class chunk> activate_chunk(chunk_type which, uint32_t id);
    //! \brief Destroy seq chunks starting from id
    void clear_chunks_since(size_t id) const noexcept;
};

MONAD_ASYNC_NAMESPACE_END
