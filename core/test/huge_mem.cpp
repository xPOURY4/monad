#include <gtest/gtest.h>

#include <monad/mem/huge_mem.hpp>

#include <iostream>

TEST(HugeMem, works)
{
    using namespace MONAD_NAMESPACE;
    HugeMem mem(5);
    std::cerr << "HugeMem construction was successful" << std::endl;
    volatile unsigned char *p = mem.get_data();
    p[0] = 5;
    std::cerr << "HugeMem write was successful" << std::endl;
}
