#include "gtest/gtest.h"

#include "monad/async/boost_fiber_wrappers.hpp"

#include "monad/core/small_prng.hpp"

#include "fuzz/one_hundred_updates.hpp"
#include "test_fixtures_gtest.hpp"

#include <monad/mpt/trie.hpp>

#include <boost/fiber/buffered_channel.hpp>

#include <vector>

namespace
{
    using namespace monad::test;
    using namespace MONAD_ASYNC_NAMESPACE;

    // static constexpr size_t MAX_CONCURRENCY = 4;

    // inline monad::io::Ring make_ring()
    //{
    //     return monad::io::Ring(MAX_CONCURRENCY, 0);
    // }
    // inline monad::io::Buffers make_buffers(monad::io::Ring &ring)
    //{
    //     return monad::io::Buffers{
    //         ring, MAX_CONCURRENCY, MAX_CONCURRENCY, 1UL << 13};
    // }

    TEST_F(OnDiskTrieGTest, single_thread_one_find_fiber)
    {
        // Populate the trie first with the 100 fixed updates
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
            [](AsyncIO *const io,
               inflight_map_t *const inflights,
               Node *const root) {
                boost::fibers::promise<monad::mpt::find_result_type> promise;
                find_request_t const request{
                    &promise, root, one_hundred_updates[0].first};
                find_notify_fiber_future(*io, *inflights, request);
                auto const [node, errc] = request.promise->get_future().get();
                ASSERT_TRUE(node != nullptr);
                EXPECT_EQ(errc, monad::mpt::find_result::success);
                EXPECT_EQ(node->leaf_view(), one_hundred_updates[0].second);
            },
            aux.io,
            &inflights,
            root.get());

        bool signal_done = false;
        boost::fibers::fiber poll_fiber(
            [](AsyncIO *const io, bool *signal_done) {
                while (!*signal_done) {
                    io->poll_nonblocking(1);
                    boost::this_fiber::sleep_for(std::chrono::milliseconds(1));
                }
            },
            aux.io,
            &signal_done);
        find_fiber.join();
        signal_done = true;
        poll_fiber.join();
    }

    // THIS is the test resembles how execution txn fibers access states in
    // triedb
    // TEST_F(OnDiskTrieGTest, spin_up_triedb_thread_do_find)
    //{
    //    // Populate the trie first with the 100 fixed updates
    //    std::vector<Update> updates;
    //    for (auto const &i : one_hundred_updates) {
    //        updates.emplace_back(make_update(i.first, i.second));
    //    }
    //    this->root = upsert_vector(
    //        this->aux, this->sm, this->root.get(), std::move(updates));
    //    EXPECT_EQ(
    //        root_hash(),
    //        0xcbb6d81afdc76fec144f6a1a283205d42c03c102a94fc210b3a1bcfdcb625884_hex);

    //    // main thread initiates fibers with find requests, hidden thread is
    //    // responsible for polling uring
    //    typedef boost::fibers::buffered_channel<find_request_t> channel_t;
    //    channel_t chan{2};
    //    // randomize the order of find requests
    //    size_t const nreq = 1000;
    //    std::vector<size_t> rand_idx(nreq);
    //    std::iota(std::begin(rand_idx), std::end(rand_idx), 0);
    //    auto rng = std::minstd_rand{};
    //    std::shuffle(std::begin(rand_idx), std::end(rand_idx), rng);

    //    inflight_map_t inflights;
    //    std::jthread triedb_thr([&](std ::stop_token token) {
    //        // create a new io instance of the same file name
    //        auto ring = make_ring();
    //        auto buf = make_buffers(ring);
    //        auto &main_pool = this->aux.io->storage_pool();
    //        AsyncIO io(main_pool, ring, buf);
    //        // when nothing to pop, poll uring, when nothing to poll, pop from
    //        // chan
    //        while (!token.stop_requested()) {
    //            find_request_t req;
    //            while (boost::fibers::channel_op_status::success ==
    //                   chan.try_pop(req)) {
    //                monad::mpt::find_notify_fiber_future(io, inflights, req);
    //            }
    //            while (io.poll_nonblocking(1)) {
    //            }
    //        }
    //        io.wait_until_done();
    //    });

    //    typedef std::pair<monad::byte_string, monad::mpt::find_result>
    //        fiber_result;

    //    auto push_request_impl = [&](size_t i) -> result<fiber_result> {
    //        ::boost::fibers::promise<monad::mpt::find_result_type> p;
    //        find_request_t req{
    //            &p, this->root.get(), one_hundred_updates[i].first};
    //        EXPECT_TRUE(
    //            chan.push(std::move(req)) !=
    //            boost::fibers::channel_op_status::closed);

    //        auto [node, errc] = p.get_future().get();
    //        if (node) {
    //            return {monad::byte_string{node->leaf_view()}, errc};
    //        }
    //        return {monad::byte_string{}, errc};
    //    };

    //    // Launch fiber tasks
    //    std::vector<::boost::fibers::future<result<fiber_result>>> futures;
    //    for (size_t i = 0; i < nreq; ++i) {
    //        futures.emplace_back(
    //            ::boost::fibers::async(push_request_impl, rand_idx[i] % 100));
    //    }
    //    for (unsigned i = 0; i < futures.size(); ++i) {
    //        auto &fut = futures[i];
    //        auto res = fut.get();
    //        // The result may contain a failure
    //        if (!res) {
    //            std::cerr << "ERROR: " << res.error().message().c_str()
    //                      << std::endl;
    //            ASSERT_TRUE(res);
    //        }
    //        // Find result may contain a failure as well
    //        if (auto errc = res.value().second; errc != find_result::success)
    //        {
    //            std::cerr << "ERROR: find node error " <<
    //            static_cast<int>(errc)
    //                      << std::endl;
    //        }
    //        else {
    //            EXPECT_EQ(res.value().second, find_result::success);
    //            EXPECT_EQ(
    //                res.value().first,
    //                one_hundred_updates[rand_idx[i] % 100].second);
    //        }
    //    }

    //    triedb_thr.request_stop();
    //    triedb_thr.join();
    //}
}
