#pragma once

#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <monad/trie/config.hpp>
#include <stdio.h>
#include <unistd.h>

MONAD_TRIE_NAMESPACE_BEGIN

namespace tr
{
    int inline tr_open(const char *const path)
    {
        int flag = O_CREAT | O_RDWR | O_DIRECT;
        int fd = open(path, flag, 0777); // TODO: configurable flag
        if (fd < 0) {
            perror("Fail to open the file.");
            exit(errno);
        }
        return fd;
    }

    void inline tr_close(int const fd)
    {
        close(fd);
    }

    // TODO: write footer
}

MONAD_TRIE_NAMESPACE_END