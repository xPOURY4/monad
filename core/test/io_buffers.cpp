#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <monad/config.hpp>

#include <gtest/gtest.h>

TEST(HugeMem, works)
{
    using namespace MONAD_NAMESPACE;
    io::Ring ring(128, 0);
    io::Buffers const buffers(ring, 8, 8);
}
