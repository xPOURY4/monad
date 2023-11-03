
#include <monad/async/storage_pool.hpp>

#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/core/assert.h>
#include <monad/core/hash.hpp>

#include <monad/async/config.hpp>
#include <monad/async/detail/start_lifetime_as_polyfill.hpp>
#include <monad/async/util.hpp>

#include <atomic>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <cassert>
#include <cstring>
#include <mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <stdlib.h>
#include <system_error>
#include <utility>
#include <vector>

#include <asm-generic/ioctl.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

MONAD_ASYNC_NAMESPACE_BEGIN

std::filesystem::path storage_pool::device::current_path() const
{
    std::filesystem::path::string_type ret;
    ret.resize(32769);
    auto *out = const_cast<char *>(ret.data());
    // Linux keeps a symlink at /proc/self/fd/n
    char in[64];
    snprintf(in, sizeof(in), "/proc/self/fd/%d", cached_readwritefd_);
    ssize_t len;
    if ((len = ::readlink(in, out, 32768)) == -1) {
        throw std::system_error(errno, std::system_category());
    }
    ret.resize(static_cast<size_t>(len));
    // Linux prepends or appends a " (deleted)" when a fd is nameless
    if (ret.size() >= 10 &&
        ((ret.compare(0, 10, " (deleted)") == 0) ||
         (ret.compare(ret.size() - 10, 10, " (deleted)") == 0))) {
        ret.clear();
    }
    return ret;
}

size_t storage_pool::device::chunks() const
{
    if (is_zoned_device()) {
        throw std::runtime_error("zonefs support isn't implemented yet");
    }
    return metadata_->chunks(size_of_file_);
}

std::pair<file_offset_t, file_offset_t> storage_pool::device::capacity() const
{
    switch (type_) {
    case device::type_t_::file: {
        struct stat stat;
        if (-1 == ::fstat(cached_readwritefd_, &stat)) {
            throw std::system_error(errno, std::system_category());
        }
        return {
            file_offset_t(stat.st_size), file_offset_t(stat.st_blocks) * 512};
    }
    case device::type_t_::block_device: {
        file_offset_t capacity, used = round_up_align<CPU_PAGE_BITS>(
                                    metadata_->total_size(size_of_file_));
        if (ioctl(
                cached_readwritefd_,
                _IOR(0x12, 114, size_t) /*BLKGETSIZE64*/,
                &capacity)) {
            throw std::system_error(errno, std::system_category());
        }
        auto const chunks = this->chunks();
        auto const useds = metadata_->chunk_bytes_used(size_of_file_);
        for (size_t n = 0; n < chunks; n++) {
            used += useds[n].load(std::memory_order_acquire);
        }
        return {capacity, used};
    }
    case device::type_t_::zoned_device:
        throw std::runtime_error("zonefs support isn't implemented yet");
    default:
        abort();
    }
}

/***************************************************************************/

storage_pool::chunk::chunk::~chunk()
{
    if (owns_readfd_ || owns_writefd_) {
        auto fd = read_fd_;
        if (owns_readfd_ && read_fd_ != -1) {
            (void)::close(read_fd_);
            read_fd_ = -1;
        }
        if (owns_writefd_ && write_fd_ != -1) {
            if (write_fd_ != fd) {
                (void)::close(write_fd_);
            }
            write_fd_ = -1;
        }
    }
}

std::pair<int, file_offset_t>
storage_pool::chunk::write_fd(size_t bytes_which_shall_be_written) noexcept
{
    if (device().is_file() || device().is_block_device()) {
        auto *metadata = device().metadata_;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device().size_of_file_);
        MONAD_DEBUG_ASSERT(
            bytes_which_shall_be_written <=
            std::numeric_limits<uint32_t>::max());
        auto size =
            (bytes_which_shall_be_written > 0)
                ? chunk_bytes_used[device_zone_id()].fetch_add(
                      static_cast<uint32_t>(bytes_which_shall_be_written),
                      std::memory_order_acq_rel)
                : chunk_bytes_used[device_zone_id()].load(
                      std::memory_order_acquire);
        MONAD_ASSERT(
            size + bytes_which_shall_be_written <= metadata->chunk_capacity);
        return std::pair<int, file_offset_t>{write_fd_, offset_ + size};
    }
    MONAD_ASSERT("zonefs support isn't implemented yet" == nullptr);
}

file_offset_t storage_pool::chunk::size() const
{
    if (device().is_file() || device().is_block_device()) {
        auto *metadata = device().metadata_;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device().size_of_file_);
        return chunk_bytes_used[device_zone_id()].load(
            std::memory_order_acquire);
    }
    throw std::runtime_error("zonefs support isn't implemented yet");
}

void storage_pool::chunk::destroy_contents()
{
    if (device().is_file()) {
        MONAD_DEBUG_ASSERT(capacity_ <= std::numeric_limits<off_t>::max());
        MONAD_DEBUG_ASSERT(offset_ <= std::numeric_limits<off_t>::max());
        if (-1 == ::fallocate(
                      write_fd_,
                      FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
                      static_cast<off_t>(offset_),
                      static_cast<off_t>(capacity_))) {
            throw std::system_error(errno, std::system_category());
        }
        auto *metadata = device().metadata_;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device().size_of_file_);
        chunk_bytes_used[device_zone_id()].store(0, std::memory_order_release);
        return;
    }
    if (device().is_block_device()) {
        uint64_t range[2] = {offset_, capacity_};
        if (ioctl(write_fd_, _IO(0x12, 119) /*BLKDISCARD*/, &range)) {
            throw std::system_error(errno, std::system_category());
        }
        auto *metadata = device().metadata_;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device().size_of_file_);
        chunk_bytes_used[device_zone_id()].store(0, std::memory_order_release);
        return;
    }
    throw std::runtime_error("zonefs support isn't implemented yet");
}

/***************************************************************************/

storage_pool::device storage_pool::make_device_(
    mode op, device::type_t_ type, std::filesystem::path const &path, int fd,
    size_t chunk_capacity)
{
    int readwritefd = fd;
    // chunk capacity must be a power of two, or Linux gets upset
    MONAD_ASSERT(
        chunk_capacity ==
        (1ULL << (63 - std::countl_zero(uint64_t(chunk_capacity)))));
    if (!path.empty()) {
        readwritefd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (-1 == readwritefd) {
            throw std::system_error(errno, std::system_category());
        }
    }
    struct stat stat;
    memset(&stat, 0, sizeof(stat));
    switch (type) {
    case device::type_t_::file:
        if (-1 == ::fstat(readwritefd, &stat)) {
            throw std::system_error(errno, std::system_category());
        }
        break;
    case device::type_t_::block_device:
        if (ioctl(
                readwritefd,
                _IOR(0x12, 114, size_t) /*BLKGETSIZE64*/,
                &stat.st_size)) {
            throw std::system_error(errno, std::system_category());
        }
        break;
    case device::type_t_::zoned_device:
        throw std::runtime_error("zonefs support isn't implemented yet");
    default:
        abort();
    }
    if (stat.st_size < 256 * 1024 * 1024 + CPU_PAGE_SIZE) {
        std::stringstream str;
        str << "Storage pool source " << path
            << " must be at least 256Mb + 4Kb long to be used with storage "
               "pool";
        throw std::runtime_error(std::move(str).str());
    }
    size_t total_size = 0;
    {
        auto *buffer =
            (std::byte *)aligned_alloc(DISK_PAGE_SIZE, DISK_PAGE_SIZE * 2);
        auto unbuffer = make_scope_exit([&]() noexcept { ::free(buffer); });
        auto const offset = round_down_align<DISK_PAGE_BITS>(
            file_offset_t(stat.st_size) - sizeof(device::metadata_t));
        MONAD_DEBUG_ASSERT(offset <= std::numeric_limits<off_t>::max());
        MONAD_DEBUG_ASSERT(static_cast<size_t>(stat.st_size) > offset);
        auto const bytesread = ::pread(
            readwritefd,
            buffer,
            static_cast<size_t>(stat.st_size) - offset,
            static_cast<off_t>(offset));
        if (-1 == bytesread) {
            throw std::system_error(errno, std::system_category());
        }
        auto *metadata_footer = start_lifetime_as<device::metadata_t>(
            buffer + bytesread - sizeof(device::metadata_t));
        if (memcmp(metadata_footer->magic, "MND0", 4) != 0 ||
            op == mode::truncate) {
            // Uninitialised
            if (op == mode::open_existing) {
                std::stringstream str;
                str << "Storage pool source " << path
                    << " has not been initialised for use with storage pool";
                throw std::runtime_error(std::move(str).str());
            }
            if (stat.st_size < 256 * 1024 * 1024 + CPU_PAGE_SIZE) {
                std::stringstream str;
                str << "Storage pool source " << path
                    << " must be at least 256Mb + 4Kb long to be initialised "
                       "for use with storage pool";
                throw std::runtime_error(std::move(str).str());
            }
            // Throw away all contents
            switch (type) {
            case device::type_t_::file:
                if (-1 == ::ftruncate(readwritefd, 0)) {
                    throw std::system_error(errno, std::system_category());
                }
                if (-1 == ::ftruncate(readwritefd, stat.st_size)) {
                    throw std::system_error(errno, std::system_category());
                }
                break;
            case device::type_t_::block_device: {
                uint64_t range[2] = {0, uint64_t(stat.st_size)};
                if (ioctl(readwritefd, _IO(0x12, 119) /*BLKDISCARD*/, &range)) {
                    throw std::system_error(errno, std::system_category());
                }
                break;
            }
            case device::type_t_::zoned_device:
                throw std::runtime_error(
                    "zonefs support isn't implemented yet");
            default:
                abort();
            }
            memset(buffer, 0, DISK_PAGE_SIZE * 2);
            memcpy(metadata_footer->magic, "MND0", 4);
            MONAD_DEBUG_ASSERT(
                chunk_capacity <= std::numeric_limits<uint32_t>::max());
            metadata_footer->chunk_capacity =
                static_cast<uint32_t>(chunk_capacity);
            ::pwrite(
                readwritefd,
                buffer,
                static_cast<size_t>(bytesread),
                static_cast<off_t>(offset));
        }
        total_size =
            metadata_footer->total_size(static_cast<size_t>(stat.st_size));
    }
    auto offset = round_down_align<CPU_PAGE_BITS>(
        static_cast<size_t>(stat.st_size) - total_size);
    auto bytestomap = round_up_align<CPU_PAGE_BITS>(
        static_cast<size_t>(stat.st_size) - offset);
    auto *addr = ::mmap(
        nullptr,
        bytestomap,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        readwritefd,
        static_cast<off_t>(offset));
    if (MAP_FAILED == addr) {
        throw std::system_error(errno, std::system_category());
    }
    auto *metadata = start_lifetime_as<device::metadata_t>(
        (std::byte *)addr + stat.st_size - offset - sizeof(device::metadata_t));
    assert(0 == memcmp(metadata->magic, "MND0", 4));
    return device(
        readwritefd, type, static_cast<size_t>(stat.st_size), metadata);
}

void storage_pool::fill_chunks_(bool interleavechunks__evenly)
{
    auto hashshouldbe = fnv1a_hash<uint32_t>::begin();
    fnv1a_hash<uint32_t>::add(hashshouldbe, 1 + interleavechunks__evenly);
    std::vector<size_t> chunks;
    size_t total = 0;
    chunks.reserve(devices_.size());
    for (auto &device : devices_) {
        if (device.is_file() || device.is_block_device()) {
            auto const devicechunks = device.chunks();
            MONAD_ASSERT(devicechunks > 0);
            MONAD_DEBUG_ASSERT(
                devicechunks <= std::numeric_limits<uint32_t>::max());
            if (devicechunks > 1) {
                chunks.push_back(devicechunks - 1);
                total += devicechunks - 1;
            }
            fnv1a_hash<uint32_t>::add(
                hashshouldbe, static_cast<uint32_t>(devicechunks));
            fnv1a_hash<uint32_t>::add(
                hashshouldbe, device.metadata_->chunk_capacity);
        }
        else {
            throw std::runtime_error("zonefs support isn't implemented yet");
        }
    }
    for (auto &device : devices_) {
        if (device.metadata_->config_hash == 0) {
            device.metadata_->config_hash = uint32_t(hashshouldbe);
        }
        else if (device.metadata_->config_hash != uint32_t(hashshouldbe)) {
            std::stringstream str;
            str << "Storage pool source " << device.current_path()
                << " was initialised with a configuration different to this "
                   "storage pool";
            throw std::runtime_error(std::move(str).str());
        }
    }
    // First block of each device goes to conventional, remainder go to
    // sequential
    chunks_[cnv].reserve(devices_.size());
    for (auto &device : devices_) {
        chunks_[cnv].emplace_back(std::weak_ptr<class chunk>{}, device, 0);
    }
    chunks_[seq].reserve(total);
    if (interleavechunks__evenly) {
        // We now need to evenly spread the sequential chunks such that if
        // device A has 20, device B has 10 and device C has 5, the interleaving
        // would be ABACABA i.e. a ratio of 4:2:1
        std::vector<double> chunkratios(chunks.size()),
            chunkcounts(chunks.size());
        for (size_t n = 0; n < chunks.size(); n++) {
            chunkratios[n] = double(total) / static_cast<double>(chunks[n]);
            chunkcounts[n] = chunkratios[n];
            chunks[n] = 1;
        }
        while (chunks_[seq].size() < chunks_[seq].capacity()) {
            for (size_t n = 0; n < chunks.size(); n++) {
                chunkcounts[n] -= 1.0;
                if (chunkcounts[n] < 0) {
                    chunks_[seq].emplace_back(
                        std::weak_ptr<class chunk>{}, devices_[n], chunks[n]++);
                    chunkcounts[n] += chunkratios[n];
                    if (chunks_[seq].size() == chunks_[seq].capacity()) {
                        break;
                    }
                }
            }
        }
#ifndef NDEBUG
        for (size_t n = 0; n < chunks.size(); n++) {
            auto devicechunks = devices_[n].chunks();
            assert(chunks[n] == devicechunks);
        }
#endif
    }
    else {
        for (size_t deviceidx = 0; deviceidx < chunks.size(); deviceidx++) {
            for (size_t n = 1; n <= chunks[deviceidx]; n++) {
                chunks_[seq].emplace_back(
                    std::weak_ptr<class chunk>{}, devices_[deviceidx], n);
            }
        }
    }
}

storage_pool::storage_pool(
    std::span<std::filesystem::path> sources, mode mode,
    bool interleavechunks__evenly)
{
    devices_.reserve(sources.size());
    for (auto &source : sources) {
        devices_.push_back([&] {
            int const fd = ::open(source.c_str(), O_PATH | O_CLOEXEC);
            if (-1 == fd) {
                throw std::system_error(errno, std::system_category());
            }
            auto unfd = make_scope_exit([fd]() noexcept { ::close(fd); });
            struct statfs statfs;
            if (-1 == ::fstatfs(fd, &statfs)) {
                throw std::system_error(errno, std::system_category());
            }
            if (statfs.f_type == 0x5a4f4653 /*ZONEFS_MAGIC*/) {
                throw std::runtime_error(
                    "zonefs support isn't actually implemented yet");
            }
            struct stat stat;
            if (-1 == ::fstat(fd, &stat)) {
                throw std::system_error(errno, std::system_category());
            }
            if ((stat.st_mode & S_IFMT) == S_IFBLK) {
                return make_device_(
                    mode, device::type_t_::block_device, source.c_str(), fd);
            }
            if ((stat.st_mode & S_IFMT) == S_IFREG) {
                return make_device_(
                    mode, device::type_t_::file, source.c_str(), fd);
            }
            std::stringstream str;
            str << "Storage pool source " << source
                << " has unknown file entry type = " << (stat.st_mode & S_IFMT);
            throw std::runtime_error(std::move(str).str());
        }());
    }
    fill_chunks_(interleavechunks__evenly);
}

storage_pool::storage_pool(use_anonymous_inode_tag, size_t chunk_capacity)
{
    int const fd = make_temporary_inode();
    auto unfd = make_scope_exit([fd]() noexcept { ::close(fd); });
    if (-1 ==
        ::ftruncate(fd, 1ULL * 1024 * 1024 * 1024 * 1024 + 24576 /* 1Tb */)) {
        throw std::system_error(errno, std::system_category());
    }
    devices_.push_back(make_device_(
        mode::truncate, device::type_t_::file, {}, fd, chunk_capacity));
    unfd.release();
    fill_chunks_(false);
}

storage_pool::~storage_pool()
{
    auto cleanupchunks_ = [&](chunk_type which) {
        for (auto &chunk_ : chunks_[which]) {
            auto chunk(chunk_.chunk.lock());
            if (chunk && (chunk->owns_readfd_ || chunk->owns_writefd_)) {
                auto fd = chunk->read_fd_;
                if (chunk->owns_readfd_ && chunk->read_fd_ != -1) {
                    (void)::close(chunk->read_fd_);
                    chunk->read_fd_ = -1;
                }
                if (chunk->owns_writefd_ && chunk->write_fd_ != -1) {
                    if (chunk->write_fd_ != fd) {
                        (void)::close(chunk->write_fd_);
                    }
                    chunk->write_fd_ = -1;
                }
            }
        }
        chunks_[which].clear();
    };
    cleanupchunks_(cnv);
    cleanupchunks_(seq);
    for (auto &device : devices_) {
        if (device.metadata_ != nullptr) {
            auto total_size =
                device.metadata_->total_size(device.size_of_file_);
            ::munmap(
                (void *)round_down_align<CPU_PAGE_BITS>(
                    (uintptr_t)device.metadata_ + sizeof(device::metadata_t) -
                    total_size),
                total_size);
        }
        if (device.uncached_readfd_ != -1) {
            (void)::close(device.uncached_readfd_);
        }
        if (device.uncached_writefd_ != -1) {
            (void)::close(device.uncached_writefd_);
        }
        if (device.cached_readwritefd_ != -1) {
            (void)::close(device.cached_readwritefd_);
        }
    }
    devices_.clear();
}

size_t storage_pool::currently_active_chunks(chunk_type which) const noexcept
{
    std::unique_lock const g(lock_);
    size_t ret = 0;
    for (auto &i : chunks_[which]) {
        if (!i.chunk.expired()) {
            ret++;
        }
    }
    return ret;
}

std::shared_ptr<class storage_pool::chunk>
storage_pool::chunk(chunk_type which, uint32_t id) const
{
    std::unique_lock const g(lock_);
    if (id >= chunks_[which].size()) {
        throw std::runtime_error("Requested chunk which does not exist");
    }
    return chunks_[which][id].chunk.lock();
}

std::shared_ptr<class storage_pool::chunk>
storage_pool::activate_chunk(chunk_type const which, uint32_t const id)
{
    std::unique_lock g(lock_);
    if (id >= chunks_[which].size()) {
        throw std::runtime_error(
            "Requested to activate chunk which does not exist");
    }
    auto ret = chunks_[which][id].chunk.lock();
    if (ret) {
        return ret;
    }
    g.unlock();
    switch (which) {
    case chunk_type::cnv:
        ret = std::shared_ptr<cnv_chunk>(new cnv_chunk(
            devices_[id],
            devices_[id].cached_readwritefd_,
            devices_[id].cached_readwritefd_,
            0,
            devices_[id].metadata_->chunk_capacity,
            id,
            false,
            false,
            false));
        break;
    case chunk_type::seq: {
        auto &chunkinfo = chunks_[chunk_type::seq][id];
        int fds[2] = {
            chunkinfo.device.uncached_readfd_,
            chunkinfo.device.uncached_writefd_};
        if (-1 == fds[0]) {
            auto devicepath = chunkinfo.device.current_path();
            if (!devicepath.empty()) {
                fds[0] = chunkinfo.device.uncached_readfd_ =
                    ::open(devicepath.c_str(), O_RDONLY | O_DIRECT | O_CLOEXEC);
                if (-1 == fds[0]) {
                    throw std::system_error(errno, std::system_category());
                }
                fds[1] = chunkinfo.device.uncached_writefd_ =
                    ::open(devicepath.c_str(), O_WRONLY | O_DIRECT | O_CLOEXEC);
                if (-1 == fds[1]) {
                    throw std::system_error(errno, std::system_category());
                }
            }
            else {
                fds[0] = fds[1] = chunkinfo.device.cached_readwritefd_;
            }
        }
        ret = std::shared_ptr<seq_chunk>(new seq_chunk(
            chunkinfo.device,
            fds[0],
            fds[1],
            file_offset_t(chunkinfo.zone_id) *
                chunkinfo.device.metadata_->chunk_capacity,
            chunkinfo.device.metadata_->chunk_capacity,
            chunkinfo.zone_id,
            false,
            false,
            true));
        break;
    }
    }
    MONAD_ASSERT(ret);
    if (ret->device().is_zoned_device()) {
        throw std::runtime_error("zonefs support isn't implemented yet");
    }
    g.lock();
    auto ret2 = chunks_[which][id].chunk.lock();
    if (ret2) {
        return ret2;
    }
    chunks_[which][id].chunk = ret;
    return ret;
}

MONAD_ASYNC_NAMESPACE_END
