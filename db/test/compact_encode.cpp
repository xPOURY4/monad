#include <monad/trie/compact_encode.hpp>

#include <gtest/gtest.h>

#include <cstring>

using namespace monad::trie;

TEST(Encoding, CompactEncode)
{
    unsigned char res[33];

    auto path = monad::byte_string{0x12, 0x34, 0x50};
    EXPECT_EQ(
        compact_encode(res, path.data(), 0, 5, false),
        monad::byte_string({0x11, 0x23, 0x45}));
    EXPECT_EQ(
        compact_encode(res, path.data(), 0, 5, true),
        monad::byte_string({0x31, 0x23, 0x45}));

    std::memset(res, 0, 33);
    path = monad::byte_string{0x01, 0x23, 0x45};
    EXPECT_EQ(
        compact_encode(res, path.data(), 0, 6, false),
        monad::byte_string({0x00, 0x01, 0x23, 0x45}));
    EXPECT_EQ(
        compact_encode(res, path.data(), 0, 6, true),
        monad::byte_string({0x20, 0x01, 0x23, 0x45}));

    std::memset(res, 0, 33);

    path = monad::byte_string({0x0f, 0x1c, 0xb8});
    EXPECT_EQ(
        compact_encode(res, path.data(), 0, 6, false),
        monad::byte_string({0x00, 0x0f, 0x1c, 0xb8}));
    EXPECT_EQ(
        compact_encode(res, path.data(), 0, 6, true),
        monad::byte_string({0x20, 0x0f, 0x1c, 0xb8}));

    EXPECT_EQ(
        compact_encode(res, path.data(), 1, 6, false),
        monad::byte_string({0x1f, 0x1c, 0xb8}));
    EXPECT_EQ(
        compact_encode(res, path.data(), 1, 6, true),
        monad::byte_string({0x3f, 0x1c, 0xb8}));

    // empty relpath
    EXPECT_EQ(
        compact_encode(res, path.data(), 6, 6, true),
        monad::byte_string({0x20}));
}