#include <monad/async/util.hpp>

#include <monad/core/assert.h>

#include <cerrno>
#include <cstdlib> // for mkstemp
#include <fcntl.h> // for open
#include <sys/user.h> // for PAGE_SIZE
#include <unistd.h> // for unlink

#if PAGE_SIZE != 4096
    #error                                                                     \
        "Non 4Kb CPU PAGE_SIZE detected. Refactoring the codebase to support that would be wise."
#endif

MONAD_ASYNC_NAMESPACE_BEGIN

int make_temporary_inode() noexcept
{
    int fd = ::open("/tmp", O_RDWR | O_TMPFILE, 0600);
    if (-1 == fd && ENOTSUP == errno) {
        // O_TMPFILE is not supported on ancient Linux kernels
        // of the kind apparently Github like to run :(
        char buffer[] = "/tmp/monad_XXXXXX";
        fd = mkstemp(buffer);
        if (-1 != fd) {
            unlink(buffer);
        }
    }
    MONAD_ASSERT(fd != -1);
    return fd;
}

MONAD_ASYNC_NAMESPACE_END
