#include <monad/core/assert.h>

#include <cstdlib>
#include <iostream>
#ifndef __clang__
#include <stacktrace>
#endif

extern const char *__progname;

void monad_assertion_failed(
    char const *const expr, char const *const function, char const *const file,
    long const line)
{
#if __cpp_lib_stacktrace >= 202011L
    std::cerr << std::stacktrace::current() << std::endl;
#endif
    std::cerr << __progname << ": " << file << ':' << line << ": " << function
              << ": Assertion '" << expr << "' failed." << std::endl;
    std::cerr << std::flush;
    std::abort();
}
