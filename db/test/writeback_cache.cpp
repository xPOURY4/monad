#include "gtest/gtest.h"

#include "monad/async/io_senders.hpp"

#include "monad/core/small_prng.hpp"

#include <deque>

namespace
{
    using namespace MONAD_ASYNC_NAMESPACE;

    TEST(AsyncIO, writeback_cache)
    {
        static constexpr size_t TEST_FILE_SIZE = 256 * 1024;
        static constexpr size_t MAX_CONCURRENCY = 4;
        const std::vector<std::byte> testfilecontents = [] {
            std::vector<std::byte> ret(TEST_FILE_SIZE);
            std::span<
                monad::small_prng::value_type,
                TEST_FILE_SIZE / sizeof(monad::small_prng::value_type)>
                s((monad::small_prng::value_type *)ret.data(),
                  TEST_FILE_SIZE / sizeof(monad::small_prng::value_type));
            monad::small_prng rand;
            for (auto &i : s) {
                i = rand();
            }
            return ret;
        }();
        monad::io::Ring testring(MAX_CONCURRENCY * 2, 0);
        monad::io::Buffers testrwbuf{
            testring,
            MAX_CONCURRENCY,
            4,
            AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
            AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};
        auto testio = std::make_unique<AsyncIO>(
            use_anonymous_inode_tag{}, testring, testrwbuf);
        struct receiver_t
        {
            enum : bool
            {
                lifetime_managed_internally = false
            };

            std::span<const std::byte> buffer;
            bool done{false};
            void set_value(
                erased_connected_operation *,
                result<std::span<const std::byte>> res)
            {
                ASSERT_TRUE(res);
                buffer = res.value();
                done = true;
            }
            void reset()
            {
                buffer = {};
                done = false;
            }
        };
        using read_state_type = AsyncIO::connected_operation_unique_ptr_type<
            read_single_buffer_sender,
            receiver_t>;
        using write_state_type = AsyncIO::connected_operation_unique_ptr_type<
            write_single_buffer_sender,
            receiver_t>;
        std::deque<read_state_type> read_states;
        monad::small_prng rand;
        for (size_t count = 0; count < 1024; count++) {
            ASSERT_NE(ftruncate(testio->get_rd_fd(), 0), -1);
            ASSERT_NE(ftruncate(testio->get_rd_fd(), TEST_FILE_SIZE), -1);
            for (size_t offset = 0; offset < TEST_FILE_SIZE;
                 offset += DISK_PAGE_SIZE * 4) {
                while (!read_states.empty() &&
                       read_states.front()->receiver().done) {
                    auto o = read_states.front()->sender().offset();
                    auto b = read_states.front()->receiver().buffer;
                    ASSERT_EQ(
                        read_states.front()->sender().buffer().size(),
                        b.size());
                    auto r =
                        memcmp(testfilecontents.data() + o, b.data(), b.size());
                    if (r != 0) {
                        std::cout << "      Checking read of " << b.size()
                                  << " bytes from offset " << o
                                  << ". Fully written is " << offset
                                  << std::endl;
                        for (auto *x = testfilecontents.data() + o,
                                  *y = b.data();
                             y < b.data() + b.size();
                             x++, y++) {
                            if (*x != *y) {
                                std::cout << "         Byte differs at index "
                                          << (y - b.data())
                                          << " shouldbe = " << (int)*x << " is "
                                          << (int)*y << std::endl;
                                break;
                            }
                        }
                        ASSERT_EQ(0, r);
                    }
                    read_states.pop_front();
                }
                std::array<write_state_type, 4> write_states{
                    testio->make_connected(
                        write_single_buffer_sender(
                            offset + 0 * DISK_PAGE_SIZE,
                            {(std::byte *)nullptr, DISK_PAGE_SIZE}),
                        receiver_t{}),
                    testio->make_connected(
                        write_single_buffer_sender(
                            offset + 1 * DISK_PAGE_SIZE,
                            {(std::byte *)nullptr, DISK_PAGE_SIZE}),
                        receiver_t{}),
                    testio->make_connected(
                        write_single_buffer_sender(
                            offset + 2 * DISK_PAGE_SIZE,
                            {(std::byte *)nullptr, DISK_PAGE_SIZE}),
                        receiver_t{}),
                    testio->make_connected(
                        write_single_buffer_sender(
                            offset + 3 * DISK_PAGE_SIZE,
                            {(std::byte *)nullptr, DISK_PAGE_SIZE}),
                        receiver_t{}),
                };
                for (size_t n = 0; n < 4; n++) {
                    memcpy(
                        write_states[n]->sender().advance_buffer_append(
                            DISK_PAGE_SIZE),
                        testfilecontents.data() + offset + n * DISK_PAGE_SIZE,
                        DISK_PAGE_SIZE);
                }
                // std::cout << "   Writing to offset " << offset << std::endl;
                for (size_t n = 0; n < 4; n++) {
                    write_states[n]->initiate();
                }
                for (size_t n = 0; n < MAX_CONCURRENCY; n++) {
                    auto r = rand();
                    auto readoffset = round_down_align<DISK_PAGE_BITS>(
                        r % (offset + DISK_PAGE_SIZE * 4));
                    auto amount = round_up_align<DISK_PAGE_BITS>(
                        r % (DISK_PAGE_SIZE * 4 + offset - readoffset));
                    amount %= AsyncIO::READ_BUFFER_SIZE;
                    if (amount > 0) {
                        read_states.push_back(testio->make_connected(
                            read_single_buffer_sender(
                                readoffset,
                                read_single_buffer_sender::buffer_type{
                                    (std::byte *)nullptr, amount}),
                            receiver_t{}));
                        read_states.back()->initiate();
                    }
                }
                testio->wait_until_done();
                for (size_t n = 0; n < 4; n++) {
                    ASSERT_TRUE(write_states[n]->receiver().done);
                    ASSERT_EQ(
                        write_states[n]->receiver().buffer.size(),
                        DISK_PAGE_SIZE);
                }
            }
        }
    }
}
