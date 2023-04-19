#pragma once

#include <errno.h>
#include <fcntl.h>
#include <monad/merkle/node.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

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

#ifdef __cplusplus
}
#endif
