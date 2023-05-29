#pragma once

#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <monad/trie/config.hpp>
#include <stdio.h>
#include <unistd.h>

MONAD_TRIE_NAMESPACE_BEGIN

class Transaction
{

    int fd_;

public:
    Transaction(const char *const path)
        : fd_([&] {
            int flag = O_CREAT | O_RDWR | O_DIRECT;
            int fd = open(path, flag, 0777); // TODO: configurable flag
            if (fd < 0) {
                perror("Fail to open the file.");
                exit(errno);
            }
            return fd;
        }())
    {
    }
    ~Transaction() { close(fd_); }

    [[gnu::always_inline]] constexpr int get_fd() const { return fd_; }
};

MONAD_TRIE_NAMESPACE_END