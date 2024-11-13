#include <stdio.h>
#include <stdlib.h>

extern char const *__progname;

void __attribute__((noreturn)) monad_compiler_assertion_failed(
    char const *expr, char const *function, char const *file, long line)
{
    fprintf(
        stderr,
        "%s: %s:%ld: %s: Assertion '%s' failed.\n",
        __progname,
        file,
        line,
        function,
        expr);

    abort();
}
