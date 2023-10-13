
#include <monad/async/storage_pool.hpp>

#include <monad/async/detail/hash.hpp>
#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/core/assert.h>

#include <cassert>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
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
    snprintf(in, sizeof(in), "/proc/self/fd/%d", _readfd);
    ssize_t len;
    if ((len = ::readlink(in, out, 32768)) == -1) {
        throw std::system_error(errno, std::system_category());
    }
    ret.resize(len);
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
    return _metadata->chunks(_size_of_file);
}

std::pair<file_offset_t, file_offset_t> storage_pool::device::capacity() const
{
    switch (_type) {
    case device::_type_t::file: {
        struct stat stat;
        if (-1 == ::fstat(_readfd, &stat)) {
            throw std::system_error(errno, std::system_category());
        }
        return {
            file_offset_t(stat.st_size), file_offset_t(stat.st_blocks) * 512};
    }
    case device::_type_t::block_device: {
        file_offset_t capacity, used = round_up_align<CPU_PAGE_BITS>(
                                    _metadata->total_size(_size_of_file));
        if (ioctl(
                _readfd, _IOR(0x12, 114, size_t) /*BLKGETSIZE64*/, &capacity)) {
            throw std::system_error(errno, std::system_category());
        }
        const auto chunks = this->chunks();
        const auto useds = _metadata->chunk_bytes_used(_size_of_file);
        for (size_t n = 0; n < chunks; n++) {
            used += useds[n].load(std::memory_order_acquire);
        }
        return {capacity, used};
    }
    case device::_type_t::zoned_device:
        throw std::runtime_error("zonefs support isn't implemented yet");
    default:
        abort();
    }
}

/***************************************************************************/

storage_pool::chunk::~chunk()
{
    if (_owns_readfd || _owns_writefd) {
        auto fd = _read_fd;
        if (_owns_readfd && _read_fd != -1) {
            (void)::close(_read_fd);
            _read_fd = -1;
        }
        if (_owns_writefd && _write_fd != -1) {
            if (_write_fd != fd) {
                (void)::close(_write_fd);
            }
            _write_fd = -1;
        }
    }
}

std::pair<int, file_offset_t>
storage_pool::chunk::write_fd(size_t bytes_which_shall_be_written) noexcept
{
    if (device().is_file() || device().is_block_device()) {
        auto *metadata = device()._metadata;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device()._size_of_file);
        auto size = chunk_bytes_used[device_zone_id()].fetch_add(
            bytes_which_shall_be_written, std::memory_order_acq_rel);
        return std::pair<int, file_offset_t>{_write_fd, _offset + size};
    }
    MONAD_ASSERT("zonefs support isn't implemented yet" == nullptr);
}

file_offset_t storage_pool::chunk::size() const
{
    if (device().is_file() || device().is_block_device()) {
        auto *metadata = device()._metadata;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device()._size_of_file);
        return chunk_bytes_used[device_zone_id()].load(
            std::memory_order_acquire);
    }
    throw std::runtime_error("zonefs support isn't implemented yet");
}

void storage_pool::chunk::destroy_contents()
{
    if (device().is_file()) {
        if (-1 == ::fallocate(
                      _write_fd,
                      FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
                      _offset,
                      _capacity)) {
            throw std::system_error(errno, std::system_category());
        }
        auto *metadata = device()._metadata;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device()._size_of_file);
        chunk_bytes_used[device_zone_id()].store(0, std::memory_order_release);
        return;
    }
    if (device().is_block_device()) {
        uint64_t range[2] = {_offset, _capacity};
        if (ioctl(_write_fd, _IO(0x12, 119) /*BLKDISCARD*/, &range)) {
            throw std::system_error(errno, std::system_category());
        }
        auto *metadata = device()._metadata;
        auto chunk_bytes_used =
            metadata->chunk_bytes_used(device()._size_of_file);
        chunk_bytes_used[device_zone_id()].store(0, std::memory_order_release);
        return;
    }
    throw std::runtime_error("zonefs support isn't implemented yet");
}

/***************************************************************************/

storage_pool::device storage_pool::_make_device(
    mode op, device::_type_t type, const std::filesystem::path &path, int fd)
{
    int readfd = fd, writefd = fd;
    if (!path.empty()) {
        readfd = ::open(path.c_str(), O_RDONLY | O_DIRECT | O_CLOEXEC);
        if (-1 == readfd) {
            throw std::system_error(errno, std::system_category());
        }
        // NOT O_DIRECT, we use this fd for the mmap
        writefd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (-1 == writefd) {
            throw std::system_error(errno, std::system_category());
        }
    }
    struct stat stat;
    memset(&stat, 0, sizeof(stat));
    switch (type) {
    case device::_type_t::file:
        if (-1 == ::fstat(writefd, &stat)) {
            throw std::system_error(errno, std::system_category());
        }
        break;
    case device::_type_t::block_device:
        if (ioctl(
                writefd,
                _IOR(0x12, 114, size_t) /*BLKGETSIZE64*/,
                &stat.st_size)) {
            throw std::system_error(errno, std::system_category());
        }
        break;
    case device::_type_t::zoned_device:
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
        const auto offset = round_down_align<DISK_PAGE_BITS>(
            file_offset_t(stat.st_size) - sizeof(device::metadata_t));
        const auto bytesread =
            ::pread(readfd, buffer, stat.st_size - offset, offset);
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
            case device::_type_t::file:
                if (-1 == ::ftruncate(writefd, 0)) {
                    throw std::system_error(errno, std::system_category());
                }
                if (-1 == ::ftruncate(writefd, stat.st_size)) {
                    throw std::system_error(errno, std::system_category());
                }
                break;
            case device::_type_t::block_device: {
                uint64_t range[2] = {0, uint64_t(stat.st_size)};
                if (ioctl(writefd, _IO(0x12, 119) /*BLKDISCARD*/, &range)) {
                    throw std::system_error(errno, std::system_category());
                }
                break;
            }
            case device::_type_t::zoned_device:
                throw std::runtime_error(
                    "zonefs support isn't implemented yet");
            default:
                abort();
            }
            memset(buffer, 0, DISK_PAGE_SIZE * 2);
            memcpy(metadata_footer->magic, "MND0", 4);
            metadata_footer->chunk_capacity = 256 * 1024 * 1024; // 256Mb
            ::pwrite(writefd, buffer, bytesread, offset);
        }
        total_size = metadata_footer->total_size(stat.st_size);
    }
    auto offset = round_down_align<CPU_PAGE_BITS>(stat.st_size - total_size);
    auto bytestomap = round_up_align<CPU_PAGE_BITS>(stat.st_size - offset);
    auto *addr = ::mmap(
        nullptr,
        bytestomap,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        writefd,
        offset);
    if (MAP_FAILED == addr) {
        throw std::system_error(errno, std::system_category());
    }
    auto *metadata = start_lifetime_as<device::metadata_t>(
        (std::byte *)addr + stat.st_size - offset - sizeof(device::metadata_t));
    assert(0 == memcmp(metadata->magic, "MND0", 4));
    return device(readfd, writefd, type, stat.st_size, metadata);
}

void storage_pool::_fill_chunks()
{
    auto hashshouldbe = fnv1a_hash<uint32_t>::begin();
    std::vector<size_t> chunks;
    size_t total = 0;
    chunks.reserve(_devices.size());
    for (auto &device : _devices) {
        if (device.is_file() || device.is_block_device()) {
            auto devicechunks = device.chunks();
            MONAD_ASSERT(devicechunks > 0);
            if (devicechunks > 1) {
                chunks.push_back(devicechunks - 1);
                total += devicechunks - 1;
            }
            fnv1a_hash<uint32_t>::add(hashshouldbe, devicechunks);
        }
        else {
            throw std::runtime_error("zonefs support isn't implemented yet");
        }
    }
    for (auto &device : _devices) {
        if (device._metadata->config_hash == 0) {
            device._metadata->config_hash = uint32_t(hashshouldbe);
        }
        else if (device._metadata->config_hash != uint32_t(hashshouldbe)) {
            std::stringstream str;
            str << "Storage pool source " << device.current_path()
                << " was initialised with a configuration different to this "
                   "storage pool";
            throw std::runtime_error(std::move(str).str());
        }
    }
    // First block of each device goes to conventional, remainder go to
    // sequential
    _chunks[cnv].reserve(_devices.size());
    for (auto &device : _devices) {
        _chunks[cnv].emplace_back(std::weak_ptr<class chunk>{}, device, 0);
    }
    // We now need to evenly spread the sequential chunks such that if
    // device A has 20, device B has 10 and device C has 5, the interleaving
    // would be ABACABA i.e. a ratio of 4:2:1
    _chunks[seq].reserve(total);
    std::vector<double> chunkratios(chunks.size()), chunkcounts(chunks.size());
    for (size_t n = 0; n < chunks.size(); n++) {
        chunkratios[n] = double(total) / chunks[n];
        chunkcounts[n] = chunkratios[n];
        chunks[n] = 1;
    }
    while (_chunks[seq].size() < _chunks[seq].capacity()) {
        for (size_t n = 0; n < chunks.size(); n++) {
            chunkcounts[n] -= 1.0;
            if (chunkcounts[n] < 0) {
                _chunks[seq].emplace_back(
                    std::weak_ptr<class chunk>{}, _devices[n], chunks[n]++);
                chunkcounts[n] += chunkratios[n];
                if (_chunks[seq].size() == _chunks[seq].capacity()) {
                    break;
                }
            }
        }
    }
#ifndef NDEBUG
    for (size_t n = 0; n < chunks.size(); n++) {
        auto devicechunks = _devices[n].chunks();
        assert(chunks[n] == devicechunks);
    }
#endif
}

storage_pool::storage_pool(std::span<std::filesystem::path> sources, mode mode)
{
    _devices.reserve(sources.size());
    for (auto &source : sources) {
        _devices.push_back([&] {
            int fd = ::open(source.c_str(), O_PATH | O_CLOEXEC);
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
                return _make_device(
                    mode, device::_type_t::block_device, source.c_str(), fd);
            }
            if ((stat.st_mode & S_IFMT) == S_IFREG) {
                return _make_device(
                    mode, device::_type_t::file, source.c_str(), fd);
            }
            std::stringstream str;
            str << "Storage pool source " << source
                << " has unknown file entry type = " << (stat.st_mode & S_IFMT);
            throw std::runtime_error(std::move(str).str());
        }());
    }
    _fill_chunks();
}

storage_pool::storage_pool(use_anonymous_inode_tag)
{
    int fd = make_temporary_inode();
    auto unfd = make_scope_exit([fd]() noexcept { ::close(fd); });
    if (-1 ==
        ::ftruncate(fd, 1ULL * 1024 * 1024 * 1024 * 1024 + 24576 /* 1Tb */)) {
        throw std::system_error(errno, std::system_category());
    }
    _devices.push_back(
        _make_device(mode::truncate, device::_type_t::file, {}, fd));
    unfd.release();
    _fill_chunks();
}

storage_pool::~storage_pool()
{
    auto cleanup_chunks = [&](chunk_type which) {
        for (auto &chunk_ : _chunks[which]) {
            auto chunk(chunk_.chunk.lock());
            if (chunk && (chunk->_owns_readfd || chunk->_owns_writefd)) {
                auto fd = chunk->_read_fd;
                if (chunk->_owns_readfd && chunk->_read_fd != -1) {
                    (void)::close(chunk->_read_fd);
                    chunk->_read_fd = -1;
                }
                if (chunk->_owns_writefd && chunk->_write_fd != -1) {
                    if (chunk->_write_fd != fd) {
                        (void)::close(chunk->_write_fd);
                    }
                    chunk->_write_fd = -1;
                }
            }
        }
        _chunks[which].clear();
    };
    cleanup_chunks(cnv);
    cleanup_chunks(seq);
    for (auto &device : _devices) {
        if (device._metadata != nullptr) {
            auto total_size =
                device._metadata->total_size(device._size_of_file);
            ::munmap(
                (void *)round_down_align<CPU_PAGE_BITS>(
                    (uintptr_t)device._metadata + sizeof(device::metadata_t) -
                    total_size),
                total_size);
        }
        if (device._readfd != -1) {
            (void)::close(device._readfd);
        }
        if (device._writefd != -1) {
            (void)::close(device._writefd);
        }
    }
    _devices.clear();
}

size_t storage_pool::currently_active_chunks(chunk_type which) const noexcept
{
    std::unique_lock g(_lock);
    size_t ret = 0;
    for (auto &i : _chunks[which]) {
        if (!i.chunk.expired()) {
            ret++;
        }
    }
    return ret;
}

std::shared_ptr<class storage_pool::chunk>
storage_pool::chunk(chunk_type which, uint32_t id) const
{
    std::unique_lock g(_lock);
    if (id >= _chunks[which].size()) {
        throw std::runtime_error("Requested chunk which does not exist");
    }
    return _chunks[which][id].chunk.lock();
}

std::shared_ptr<class storage_pool::chunk>
storage_pool::activate_chunk(const chunk_type which, const uint32_t id)
{
    std::unique_lock g(_lock);
    if (id >= _chunks[which].size()) {
        throw std::runtime_error(
            "Requested to activate chunk which does not exist");
    }
    auto ret = _chunks[which][id].chunk.lock();
    if (ret) {
        return ret;
    }
    g.unlock();
    switch (which) {
    case chunk_type::cnv:
        ret = std::shared_ptr<cnv_chunk>(new cnv_chunk(
            _devices[id],
            _devices[id]._readfd,
            _devices[id]._writefd,
            0,
            _devices[id]._metadata->chunk_capacity,
            id,
            false,
            false,
            false));
        break;
    case chunk_type::seq: {
        auto &chunkinfo = _chunks[chunk_type::seq][id];
        int directfd = chunkinfo.device._writefd;
        auto devicepath = chunkinfo.device.current_path();
        if (!devicepath.empty()) {
            directfd =
                ::open(devicepath.c_str(), O_WRONLY | O_DIRECT | O_CLOEXEC);
            if (-1 == directfd) {
                throw std::system_error(errno, std::system_category());
            }
        }
        auto undirectfd = make_scope_exit([&]() noexcept {
            if (chunkinfo.device._writefd != directfd) {
                ::close(directfd);
            }
        });
        ret = std::shared_ptr<seq_chunk>(new seq_chunk(
            chunkinfo.device,
            chunkinfo.device._readfd,
            directfd,
            file_offset_t(chunkinfo.zone_id) *
                chunkinfo.device._metadata->chunk_capacity,
            chunkinfo.device._metadata->chunk_capacity,
            chunkinfo.zone_id,
            false,
            chunkinfo.device._writefd != directfd,
            true));
        undirectfd.release();
        break;
    }
    }
    MONAD_ASSERT(ret);
    if (ret->device().is_zoned_device()) {
        throw std::runtime_error("zonefs support isn't implemented yet");
    }
    g.lock();
    auto ret2 = _chunks[which][id].chunk.lock();
    if (ret2) {
        return ret2;
    }
    _chunks[which][id].chunk = ret;
    return ret;
}

MONAD_ASYNC_NAMESPACE_END
