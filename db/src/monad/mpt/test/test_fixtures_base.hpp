#pragma once

#include <monad/mpt/compute.hpp>
#include <monad/mpt/trie.hpp>

#include <monad/core/small_prng.hpp>

#include <array>
#include <vector>

namespace monad::test
{
    using namespace monad::mpt;
    using namespace monad::literals;

    struct DummyComputeLeafData
    {
        // TEMPORARY for POC
        // compute leaf data as - concat(input_leaf, hash);
        static byte_string compute(Node const &node)
        {
            return byte_string{node.value()} + byte_string{node.data()};
        }
    };

    using MerkleCompute = ::monad::mpt::MerkleComputeBase<DummyComputeLeafData>;

    struct EmptyCompute final : Compute
    {
        virtual unsigned
        compute_len(std::span<ChildData> const, uint16_t const) override
        {
            return 0;
        }

        virtual unsigned compute_branch(unsigned char *const, Node *) override
        {
            return 0;
        }

        virtual unsigned compute(unsigned char *const, Node *) override
        {
            return 0;
        }
    };

    class StateMachineWithBlockNo final : public TrieStateMachine
    {
    private:
        enum class TrieSection : uint8_t
    {
        BlockNo = 0,
        Account,
        Storage,
        Receipt, // not used yet
        Invalid
    }  default_section_, curr_section_;

        static std::pair<Compute &, Compute &> candidate_computes()
        {
            // candidate impls to use
            static MerkleCompute m{};
            static EmptyCompute e{};
            return {m, e};
        }

    public:
        StateMachineWithBlockNo(uint8_t const sec = 0)
            : default_section_(static_cast<TrieSection>(sec))
            , curr_section_(default_section_)
        {
        }

        virtual std::unique_ptr<TrieStateMachine> clone() const override
        {
            auto cloned = std::make_unique<StateMachineWithBlockNo>(
                static_cast<uint8_t>(default_section_));
            cloned->reset(this->get_state());
            return cloned;
        }

        virtual void reset(std::optional<uint8_t> sec) override
        {
            curr_section_ = sec.has_value()
                                ? static_cast<TrieSection>(sec.value())
                                : default_section_;
        }

        virtual void forward(byte_string_view = {}) override
        {
            switch (curr_section_) {
            case (TrieSection::BlockNo):
                curr_section_ = TrieSection::Account;
                break;
            case (TrieSection::Account):
                curr_section_ = TrieSection::Storage;
                break;
            default:
                curr_section_ = TrieSection::Invalid;
            }
        }

        virtual void backward() override
        {
            switch (curr_section_) {
            case (TrieSection::Storage):
                curr_section_ = TrieSection::Account;
                break;
            case (TrieSection::Account):
                curr_section_ = TrieSection::BlockNo;
                break;
            default:
                curr_section_ = TrieSection::Invalid;
            }
        }

        virtual constexpr Compute &get_compute() override
        {
            if (curr_section_ == TrieSection::BlockNo) {
                return candidate_computes().second;
            }
            else {
                return candidate_computes().first;
            }
        }

        virtual constexpr Compute &get_compute(uint8_t sec) override
        {
            auto section = static_cast<TrieSection>(sec);
            if (section == TrieSection::BlockNo) {
                return candidate_computes().second;
            }
            else {
                return candidate_computes().first;
            }
        }

        virtual constexpr uint8_t get_state() const override
        {
            return static_cast<uint8_t>(curr_section_);
        }

        virtual constexpr CacheOption get_cache_option() const override
        {
            switch (curr_section_) {
            case (TrieSection::BlockNo):
                return CacheOption::CacheAll;
            case (TrieSection::Account):
                return CacheOption::ApplyLevelBasedCache;
            default:
                return CacheOption::DisposeAll;
            }
        }
    };

    static_assert(sizeof(StateMachineWithBlockNo) == 16);
    static_assert(alignof(StateMachineWithBlockNo) == 8);

    class StateMachineAlwaysEmpty final : public TrieStateMachine
    {
        static Compute &candidate_computes()
        {
            // candidate impls to use
            static EmptyCompute e{};
            return e;
        }

    public:
        StateMachineAlwaysEmpty() = default;

        virtual std::unique_ptr<TrieStateMachine> clone() const override
        {
            return std::make_unique<StateMachineAlwaysEmpty>();
        }

        virtual void reset(std::optional<uint8_t>) override {}

        virtual void forward(monad::byte_string_view = {}) override {}

        virtual void backward() override {}

        virtual Compute &get_compute() override
        {
            return candidate_computes();
        }

        virtual Compute &get_compute(uint8_t) override
        {
            return candidate_computes();
        }

        virtual constexpr uint8_t get_state() const override
        {
            return 0;
        }

        virtual constexpr CacheOption get_cache_option() const override
        {
            return CacheOption::CacheAll;
        }
    };

    Node::UniquePtr upsert_vector(
        UpdateAux &aux, TrieStateMachine &sm, Node::UniquePtr old,
        std::vector<Update> &&update_vec)
    {
        UpdateList update_ls;
        for (auto &it : update_vec) {
            update_ls.push_front(it);
        }
        return upsert(aux, sm, std::move(old), std::move(update_ls));
    }

    template <class... Updates>
    [[nodiscard]] constexpr Node::UniquePtr upsert_updates(
        UpdateAux &aux, TrieStateMachine &sm, Node::UniquePtr old,
        Updates... updates)
    {
        UpdateList update_ls;
        (update_ls.push_front(updates), ...);
        return upsert(aux, sm, std::move(old), std::move(update_ls));
    }

    namespace fixed_updates
    {
        std::vector<std::pair<monad::byte_string, monad::byte_string>> const kv{
            {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
            {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
            {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
             0xdeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
            {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};
    };

    namespace unrelated_leaves
    {
        std::vector<std::pair<monad::byte_string, monad::byte_string>> const kv{
            {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
            {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
            {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
            {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};
    };

    namespace var_len_values
    {
        std::vector<std::pair<monad::byte_string, monad::byte_string>> const kv{
            {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdead_hex}, // 0
            {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
             0xbeef_hex}, // 1
            {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
             0xba_hex}, // 2
            {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeef_hex}, // 3
            {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeefcafe_hex}, // 4
            {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
             0xdeadcafedeadcafedeadcafedeadcafedead_hex}, // 5
            {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbabedeadbabedeadbabedead_hex}}; // 6
    };

    // merkle tries
    template <class Base>
    class InMemoryTrieBase : public Base
    {
    public:
        Node::UniquePtr root;
        UpdateAux aux;

        InMemoryTrieBase()
            : root()
            , aux(nullptr)
        {
        }

        void reset()
        {
            root.reset();
        }

        constexpr bool on_disk() const
        {
            return aux.is_on_disk();
        }

        constexpr MONAD_ASYNC_NAMESPACE::storage_pool *get_storage_pool() const
        {
            return nullptr;
        }
    };

    template <class Base>
    class OnDiskTrieBase : public Base
    {
    private:
        monad::async::storage_pool pool{
            monad::async::use_anonymous_inode_tag{}};
        monad::io::Ring ring;
        monad::io::Buffers rwbuf;
        MONAD_ASYNC_NAMESPACE::AsyncIO io;

    public:
        Node::UniquePtr root;
        UpdateAux aux;

        OnDiskTrieBase()
            : ring(monad::io::Ring(2, 0))
            , rwbuf(
                  ring, 2, 4,
                  MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
                  MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE)
            , io(pool, ring, rwbuf)
            , root()
            , aux(&io)
        {
        }

        void reset()
        {
            root.reset();
        }

        constexpr bool on_disk() const
        {
            return aux.is_on_disk();
        }

        constexpr MONAD_ASYNC_NAMESPACE::storage_pool *get_storage_pool()
        {
            return &io.storage_pool();
        }
    };

    template <class Base>
    class MerkleTrie : public Base
    {
    public:
        StateMachineWithBlockNo sm{1};

        monad::byte_string root_hash()
        {
            if (this->root.get()) {
                monad::byte_string res(32, 0);
                auto const len = this->sm.get_compute().compute(
                    res.data(), this->root.get());
                if (len < KECCAK256_SIZE) {
                    keccak256(res.data(), len, res.data());
                }
                return res;
            }
            return empty_trie_hash;
        }
    };

    template <class Base>
    class PlainTrie : public Base
    {
    public:
        StateMachineAlwaysEmpty sm;
    };

    template <typename BaseTrie>
    class EraseFixture : public BaseTrie
    {
    public:
        EraseFixture()
            : BaseTrie()
        {
            auto const &kv = fixed_updates::kv;

            std::vector<Update> update_vec;
            std::ranges::transform(
                kv, std::back_inserter(update_vec), [](auto &su) -> Update {
                    auto &[k, v] = su;
                    return make_update(k, v);
                });
            this->root = upsert_vector(
                this->aux, this->sm, nullptr, std::move(update_vec));
        }
    };

    template <
        size_t chunks_to_fill, bool alternate_slow_fast_writer, class Base>
    struct FillDBWithChunks : public Base
    {
        struct state_t
        {
            MONAD_ASYNC_NAMESPACE::storage_pool pool{
                MONAD_ASYNC_NAMESPACE::use_anonymous_inode_tag{},
                MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};
            monad::io::Ring ring{1, 0};
            monad::io::Buffers rwbuf{
                ring,
                2,
                4,
                MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
                MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};
            MONAD_ASYNC_NAMESPACE::AsyncIO io{pool, ring, rwbuf};
            MerkleCompute comp;
            Node::UniquePtr root;
            StateMachineWithBlockNo sm{1};
            UpdateAux aux{&io}; // trie section starts from account
            monad::small_prng rand;
            std::vector<std::pair<monad::byte_string, size_t>> keys;

            state_t()
            {
                aux.alternate_slow_fast_node_writer_unit_testing_only(
                    alternate_slow_fast_writer);
                ensure_total_chunks(chunks_to_fill);
                std::cout << "After suite set up before testing:";
                print(std::cout);
            }

            std::ostream &print(std::ostream &s) const
            {
                auto v = pool.devices().front().capacity();
                std::cout << "\n   Storage pool capacity = " << v.first
                          << " consumed = " << v.second
                          << " chunks = " << pool.chunks(pool.seq);
                auto const diff =
                    (int64_t(aux.get_lower_bound_free_space()) -
                     int64_t(v.first - v.second));
                std::cout << "\n   DB thinks there is a lower bound of "
                          << aux.get_lower_bound_free_space()
                          << " bytes free whereas the syscall thinks there is "
                          << (v.first - v.second)
                          << " bytes free, which is a difference of " << diff
                          << ".\n";
                std::cout << "   Fast list:";
                for (auto const *ci = aux.db_metadata()->fast_list_begin();
                     ci != nullptr;
                     ci = ci->next(aux.db_metadata())) {
                    auto idx = ci->index(aux.db_metadata());
                    auto chunk = pool.chunk(pool.seq, idx);
                    std::cout << "\n      Chunk " << idx
                              << " has capacity = " << chunk->capacity()
                              << " consumed = " << chunk->size();
                    auto chunk_offset_id = aux.chunk_id_from_insertion_count(
                        UpdateAux::chunk_list::fast, ci->insertion_count());
                    MONAD_ASSERT(chunk_offset_id == idx);
                }
                std::cout << "\n\n   Slow list:";
                for (auto const *ci = aux.db_metadata()->slow_list_begin();
                     ci != nullptr;
                     ci = ci->next(aux.db_metadata())) {
                    auto idx = ci->index(aux.db_metadata());
                    auto chunk = pool.chunk(pool.seq, idx);
                    std::cout << "\n      Chunk " << idx
                              << " has capacity = " << chunk->capacity()
                              << " consumed = " << chunk->size();
                    auto chunk_offset_id = aux.chunk_id_from_insertion_count(
                        UpdateAux::chunk_list::slow, ci->insertion_count());
                    MONAD_ASSERT(chunk_offset_id == idx);
                }
                std::cout << std::endl;
                return s;
            }

            void ensure_total_chunks(size_t chunks)
            {
                std::vector<Update> updates;
                updates.reserve(1000);
                for (;;) {
                    UpdateList update_ls;
                    updates.clear();
                    for (size_t n = 0; n < 1000; n++) {
                        {
                            monad::byte_string key(
                                0x1234567812345678123456781234567812345678123456781234567812345678_hex);
                            for (size_t n = 0; n < key.size(); n += 4) {
                                *(uint32_t *)(key.data() + n) = rand();
                            }
                            keys.emplace_back(
                                std::move(key), aux.get_root_offset().id);
                        }
                        updates.push_back(
                            make_update(keys.back().first, keys.back().first));
                        update_ls.push_front(updates.back());
                    }
                    root =
                        upsert(aux, sm, std::move(root), std::move(update_ls));
                    size_t count = 0;
                    for (auto const *ci = aux.db_metadata()->fast_list_begin();
                         ci != nullptr;
                         count++, ci = ci->next(aux.db_metadata())) {
                    }
                    if (count >= chunks) {
                        break;
                    }
                }
            }

            std::vector<
                std::pair<uint32_t, MONAD_MPT_NAMESPACE::detail::unsigned_20>>
            fast_list_ids() const
            {
                std::vector<std::pair<
                    uint32_t,
                    MONAD_MPT_NAMESPACE::detail::unsigned_20>>
                    ret;
                ret.reserve(4);
                for (auto const *ci = aux.db_metadata()->fast_list_begin();
                     ci != nullptr;
                     ci = ci->next(aux.db_metadata())) {
                    ret.emplace_back(
                        ci->index(aux.db_metadata()), ci->insertion_count());
                }
                return ret;
            }

            std::vector<
                std::pair<uint32_t, MONAD_MPT_NAMESPACE::detail::unsigned_20>>
            slow_list_ids() const
            {
                std::vector<std::pair<
                    uint32_t,
                    MONAD_MPT_NAMESPACE::detail::unsigned_20>>
                    ret;
                ret.reserve(4);
                for (auto const *ci = aux.db_metadata()->slow_list_begin();
                     ci != nullptr;
                     ci = ci->next(aux.db_metadata())) {
                    ret.emplace_back(
                        ci->index(aux.db_metadata()), ci->insertion_count());
                }
                return ret;
            }

            monad::byte_string root_hash()
            {
                if (this->root.get()) {
                    monad::byte_string res(32, 0);
                    this->sm.get_compute().compute(
                        res.data(), this->root.get());
                    return res;
                }
                return empty_trie_hash;
            }
        };

        static state_t *&state()
        {
            static state_t *v;
            return v;
        }

        static void SetUpTestSuite()
        {
            state() = new state_t;
        }

        static void TearDownTestSuite()
        {
            delete state();
        }
    };

}
