#include <monad/mpt/db.hpp>

#include <monad/async/config.hpp>
#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/async/io.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/result.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/ondisk_db_config.hpp>
#include <monad/mpt/traverse.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/update.hpp>

#include <cerrno>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

#include <fcntl.h>
#include <unistd.h>

// TODO unstable paths between versions
#if __has_include(<boost/outcome/experimental/status-code/generic_code.hpp>)
    #include <boost/outcome/experimental/status-code/generic_code.hpp>
#else
    #include <boost/outcome/experimental/status-code/status-code/generic_code.hpp>
#endif

#undef BLOCK_SIZE // without this concurrentqueue.h gets sad
#include "concurrentqueue.h"

MONAD_MPT_NAMESPACE_BEGIN

template <class T>
using concurrent_queue = ::moodycamel::ConcurrentQueue<T>;

struct Db::OnDisk
{
    struct fiber_upsert_request_t
    {
        threadsafe_boost_fibers_promise<Node::UniquePtr> &promise;
        Node::UniquePtr prev_root;
        StateMachine &sm;
        UpdateList &&updates;
        uint64_t const version;
        bool const enable_compaction;
    };

    using comms_type = std::variant<
        std::monostate, fiber_find_request_t, fiber_upsert_request_t>;
    concurrent_queue<comms_type> comms;

    std::mutex lock;
    std::condition_variable cond;

    struct triedb_worker
    {
        OnDisk *parent;
        UpdateAuxImpl &aux;

        async::storage_pool pool;
        io::Ring ring;
        io::Buffers rwbuf;
        async::AsyncIO io;
        bool const compaction;
        std::atomic<bool> sleeping{false}, done{false};

        triedb_worker(
            OnDisk *parent_, UpdateAuxImpl &aux_, OnDiskDbConfig const &options)
            : parent(parent_)
            , aux(aux_)
            , pool{[&] -> async::storage_pool {
                if (options.dbname_paths.empty()) {
                    return async::storage_pool{
                        async::use_anonymous_inode_tag{}};
                }
                // initialize db file on disk
                for (auto const &dbname_path : options.dbname_paths) {
                    if (!std::filesystem::exists(dbname_path)) {
                        int const fd = ::open(
                            dbname_path.c_str(),
                            O_CREAT | O_RDWR | O_CLOEXEC,
                            0600);
                        if (-1 == fd) {
                            throw std::system_error(
                                errno, std::system_category());
                        }
                        auto unfd = monad::make_scope_exit(
                            [fd]() noexcept { ::close(fd); });
                        if (-1 ==
                            ::ftruncate(
                                fd,
                                options.file_size_db * 1024 * 1024 * 1024 +
                                    24576)) {
                            throw std::system_error(
                                errno, std::system_category());
                        }
                    }
                }
                return async::storage_pool{
                    options.dbname_paths,
                    options.append ? async::storage_pool::mode::open_existing
                                   : async::storage_pool::mode::truncate};
            }()}
            , ring{io::Ring{options.uring_entries, options.sq_thread_cpu}}
            , rwbuf{ring, options.rd_buffers, options.wr_buffers, async::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE, async::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE}
            , io{pool, ring, rwbuf}
            , compaction{options.compaction}
        {
        }

        // Runs in the triedb worker thread
        void run()
        {
            inflight_map_t inflights;
            /* In case you're wondering why we use a vector for a single
            element, it's because for some odd reason the MoodyCamel concurrent
            queue only supports move only types via its iterator interface. No
            that makes no sense to me either, but it is what it is.
            */
            std::vector<comms_type> request;
            request.reserve(1);
            unsigned did_nothing_count = 0;
            while (!done.load(std::memory_order_acquire)) {
                bool did_nothing = true;
                request.clear();
                if (parent->comms.try_dequeue_bulk(
                        std::back_inserter(request), 1) > 0) {
                    if (auto *req = std::get_if<1>(&request.front());
                        req != nullptr) {
                        find_notify_fiber_future(aux, inflights, *req);
                    }
                    else if (auto *req = std::get_if<2>(&request.front());
                             req != nullptr) {
                        req->promise.set_value(aux.do_update(
                            std::move(req->prev_root),
                            req->sm,
                            std::move(req->updates),
                            req->version,
                            compaction && req->enable_compaction));
                    }
                    did_nothing = false;
                }
                io.poll_nonblocking(1);
                boost::this_fiber::yield();
                if (boost::fibers::has_ready_fibers()) {
                    did_nothing = false;
                }
                if (did_nothing && io.io_in_flight() > 0) {
                    did_nothing = false;
                }
                if (did_nothing) {
                    did_nothing_count++;
                }
                else {
                    did_nothing_count = 0;
                }
                if (did_nothing_count > 1000000) {
                    std::unique_lock g(parent->lock);
                    sleeping.store(true, std::memory_order_release);
                    /* Very irritatingly, Boost.Fiber may have fibers scheduled
                     which weren't ready before, and if we sleep forever here
                     then they never run and cause anything waiting on them to
                     hang. So pulse Boost.Fiber every second at most for those
                     extremely rare occasions.
                     */
                    parent->cond.wait_for(g, std::chrono::seconds(1), [this] {
                        return done.load(std::memory_order_acquire) ||
                               parent->comms.size_approx() > 0;
                    });
                    sleeping.store(false, std::memory_order_release);
                }
            }
        }
    };

    std::unique_ptr<triedb_worker> worker;
    std::thread worker_thread;

    OnDisk(UpdateAuxImpl &aux, OnDiskDbConfig const &options)
        : worker_thread([&] {
            {
                std::unique_lock g(lock);
                worker = std::make_unique<triedb_worker>(this, aux, options);
            }
            worker->run();
            std::unique_lock g(lock);
            worker.reset();
        })
    {
        comms.enqueue({});
        while (comms.size_approx() > 0) {
            std::this_thread::yield();
        }
        std::unique_lock g(lock);
        MONAD_ASSERT(worker);
    }

    ~OnDisk()
    {
        {
            std::unique_lock g(lock);
            worker->done.store(true, std::memory_order_release);
            cond.notify_one();
        }
        worker_thread.join();
    }

    // threadsafe
    find_result_type find_fiber_blocking(NodeCursor start, NibblesView key)
    {
#if 0
        // Do speculative check of the cache before going to concurrent queue
        // Disabled for now as UpdateAux would need mutex configured
            struct receiver_t
            {
                std::optional<find_request_sender::result_type::value_type> out;

                void set_value(
                    MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
                    find_request_sender::result_type res)
                {
                    MONAD_ASSERT(res);
                    out = std::move(res).assume_value();
                }
            };

            auto g(worker->aux.shared_lock());
            auto state = MONAD_ASYNC_NAMESPACE::connect(
                find_request_sender(
                    worker->aux, root, key, opt_node_prefix_index),
                receiver_t{});
            // This will complete immediately, as we are not on the triedb
            // thread
            state.initiate();
            MONAD_ASSERT(state.receiver().out.has_value());
            auto const [node, result] = *state.receiver().out;
            if (result != find_result::need_to_initiate_in_io_thread) {
                return {node, result};
            }
#endif
        threadsafe_boost_fibers_promise<find_result_type> promise;
        fiber_find_request_t req{
            .promise = &promise, .start = start, .key = key};
        comms.enqueue(req);
        if (worker->sleeping.load(std::memory_order_acquire)) {
            std::unique_lock g(lock);
            cond.notify_one();
        }
        return promise.get_future().get();
    }

    // threadsafe
    Node::UniquePtr upsert_fiber_blocking(
        Node::UniquePtr prev_root, StateMachine &sm, UpdateList &&updates,
        uint64_t const version, bool const enable_compaction)
    {
        threadsafe_boost_fibers_promise<Node::UniquePtr> promise;
        comms.enqueue(fiber_upsert_request_t{
            .promise = promise,
            .prev_root = std::move(prev_root),
            .sm = sm,
            .updates = std::move(updates),
            .version = version,
            .enable_compaction = enable_compaction});
        if (worker->sleeping.load(std::memory_order_acquire)) {
            std::unique_lock g(lock);
            cond.notify_one();
        }
        return promise.get_future().get();
    }
};

Db::Db(StateMachine &machine)
    : aux_{nullptr}
    , root_{}
    , machine_{machine}
{
}

Db::Db(StateMachine &machine, OnDiskDbConfig const &config)
    : on_disk_{std::make_unique<OnDisk>(aux_, config)}
    , aux_{&on_disk_->worker->io}
    , root_(
          config.append ? Node::UniquePtr{read_node_blocking(
                              on_disk_->worker->pool, aux_.get_root_offset())}
                        : Node::UniquePtr{})
    , machine_{machine}
{
    MONAD_DEBUG_ASSERT(aux_.is_on_disk());
    if (config.append) {
        MONAD_ASSERT(root_);
    }
}

Db::~Db()
{
    // Force a synchronisation
    aux_.unique_lock();
}

Result<NodeCursor> Db::get(NodeCursor root, NibblesView const key)
{
    auto const [it, result] = (on_disk_ != nullptr)
                                  ? on_disk_->find_fiber_blocking(root, key)
                                  : find_blocking(aux_, root, key);
    if (result != find_result::success) {
        return system_error2::errc::no_such_file_or_directory;
    }
    MONAD_DEBUG_ASSERT(it.node != nullptr);
    MONAD_DEBUG_ASSERT(it.node->has_value());
    return it;
}

Result<byte_string_view> Db::get(NibblesView const key, uint64_t const block_id)
{
    auto res = get(root(), serialize_as_big_endian<BLOCK_NUM_BYTES>(block_id));
    if (!res.has_value()) {
        return system_error2::errc::no_such_file_or_directory;
    }
    res = get(res.value(), key);
    if (!res.has_value()) {
        return system_error2::errc::no_such_file_or_directory;
    }
    return res.value().node->value();
}

Result<byte_string_view> Db::get_data(NodeCursor root, NibblesView const key)
{
    auto res = get(root, key);
    if (!res.has_value()) {
        return system_error2::errc::no_such_file_or_directory;
    }
    MONAD_DEBUG_ASSERT(res.value().node != nullptr);

    return res.value().node->data();
}

Result<byte_string_view>
Db::get_data(NibblesView const key, uint64_t const block_id)
{
    auto res = get(root(), serialize_as_big_endian<BLOCK_NUM_BYTES>(block_id));
    if (!res.has_value()) {
        return system_error2::errc::no_such_file_or_directory;
    }
    return get_data(res.value(), key);
}

void Db::upsert(
    UpdateList list, uint64_t const block_id, bool const enable_compaction)
{
    root_ =
        (on_disk_ != nullptr)
            ? on_disk_->upsert_fiber_blocking(
                  std::move(root_),
                  machine_,
                  std::move(list),
                  block_id,
                  enable_compaction)
            : aux_.do_update(
                  std::move(root_), machine_, std::move(list), block_id, false);
}

void Db::traverse(
    NibblesView const prefix, TraverseMachine &machine, uint64_t const block_id)
{
    auto const block_id_prefix =
        serialize_as_big_endian<BLOCK_NUM_BYTES>(block_id);
    auto res = get(root(), NibblesView{block_id_prefix});
    MONAD_ASSERT(res.has_value());
    res = get(res.value(), prefix);
    if (!res.has_value()) {
        return;
    }
    auto *node = res.value().node;
    MONAD_DEBUG_ASSERT(node != nullptr);
    preorder_traverse(aux_, *node, machine);
}

NodeCursor Db::root() noexcept
{
    return root_ ? NodeCursor{*root_} : NodeCursor{};
}

MONAD_MPT_NAMESPACE_END
