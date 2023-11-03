#include "gtest/gtest.h"

#include "fuzz/one_hundred_updates.hpp"

#include "test_fixtures_base.hpp"
#include "test_fixtures_gtest.hpp"

#include <monad/async/concepts.hpp>
#include <monad/async/config.hpp>
#include <monad/async/io.hpp>
#include <monad/core/byte_string.hpp>
#include <monad/core/hex_literal.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/trie.hpp>


#include <boost/fiber/channel_op_status.hpp>
#include <boost/fiber/future/async.hpp>
#include <boost/fiber/future/future.hpp>
#include <boost/fiber/future/future_status.hpp>
#include <boost/fiber/future/promise.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <optional>
#include <ostream>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace monad::test;
    using namespace MONAD_ASYNC_NAMESPACE;

    void find(
        AsyncIO *const io, inflight_map_t *const inflights, Node *const root,
        monad::byte_string_view const key, monad::byte_string_view const value)
    {
        boost::fibers::promise<monad::mpt::find_result_type> promise;
        find_request_t const request{
            .promise = &promise,
            .root = root,
            .key = key,
            .node_pi = std::nullopt};
        find_notify_fiber_future(*io, *inflights, request);
        auto const [node, errc] = request.promise->get_future().get();
        ASSERT_TRUE(node != nullptr);
        EXPECT_EQ(errc, monad::mpt::find_result::success);
        EXPECT_EQ(node->leaf_view(), value);
    };

    void poll(AsyncIO *const io, bool *signal_done)
    {
        while (!*signal_done) {
            io->poll_nonblocking(1);
            boost::this_fiber::sleep_for(std::chrono::milliseconds(1));
        }
    };

    TEST_F(OnDiskTrieGTest, single_thread_one_find_fiber)
    {
        std::vector<Update> updates;
        for (auto const &i : one_hundred_updates) {
            updates.emplace_back(make_update(i.first, i.second));
        }
        this->root = upsert_vector(
            this->aux, this->sm, this->root.get(), std::move(updates));
        EXPECT_EQ(
            root_hash(),
            0xcbb6d81afdc76fec144f6a1a283205d42c03c102a94fc210b3a1bcfdcb625884_hex);

        inflight_map_t inflights;
        boost::fibers::fiber find_fiber(
            find,
            aux.io,
            &inflights,
            root.get(),
            one_hundred_updates[0].first,
            one_hundred_updates[0].second);
        bool signal_done = false;
        boost::fibers::fiber poll_fiber(poll, aux.io, &signal_done);
        find_fiber.join();
        signal_done = true;
        poll_fiber.join();
    }

    TEST_F(OnDiskTrieGTest, single_thread_one_hundred_find_fibers)
    {
        std::vector<Update> updates;
        for (auto const &i : one_hundred_updates) {
            updates.emplace_back(make_update(i.first, i.second));
        }
        this->root = upsert_vector(
            this->aux, this->sm, this->root.get(), std::move(updates));
        EXPECT_EQ(
            root_hash(),
            0xcbb6d81afdc76fec144f6a1a283205d42c03c102a94fc210b3a1bcfdcb625884_hex);

        inflight_map_t inflights;
        std::vector<boost::fibers::fiber> fibers;
        for (auto const &[key, val] : one_hundred_updates) {
            fibers.emplace_back(find, aux.io, &inflights, root.get(), key, val);
        }

        bool signal_done = false;
        boost::fibers::fiber poll_fiber(poll, aux.io, &signal_done);

        for (auto &fiber : fibers) {
            fiber.join();
        }
        signal_done = true;
        poll_fiber.join();
    }
}
