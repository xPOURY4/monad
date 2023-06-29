#pragma once

#include <monad/trie/config.hpp>

#include <filesystem>

#include <fcntl.h>
#include <unistd.h>

MONAD_TRIE_NAMESPACE_BEGIN

class Transaction
{
    int fd_;

public:
    Transaction(const std::filesystem::path &path)
        : fd_([&] {
            int flag = O_CREAT | O_RDWR | O_DIRECT;
            int fd = open(path.c_str(), flag, 0777); // TODO: configurable flag
            MONAD_ASSERT(fd != -1);
            return fd;
        }())
    {
    }
    ~Transaction()
    {
        close(fd_);
        fd_ = -1;
    }

    constexpr int get_fd() const noexcept
    {
        return fd_;
    }
};

MONAD_TRIE_NAMESPACE_END