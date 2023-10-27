#pragma once

#include "gtest/gtest.h"

#include <monad/mpt/compute.hpp>
#include <monad/mpt/trie.hpp>

#include <array>
#include <vector>

namespace monad::test
{
    using namespace monad::mpt;
    using namespace monad::literals;

    node_ptr upsert_vector(
        UpdateAux &update_aux, Node *const old,
        std::vector<Update> &&update_vec)
    {
        UpdateList update_ls;
        for (auto &it : update_vec) {
            update_ls.push_front(it);
        }
        return upsert(update_aux, old, std::move(update_ls));
    }

    template <class... Updates>
    [[nodiscard]] constexpr node_ptr
    upsert_updates(UpdateAux &update_aux, Node *const old, Updates... updates)
    {
        UpdateList update_ls;
        (update_ls.push_front(updates), ...);
        return upsert(update_aux, old, std::move(update_ls));
    }

    namespace fixed_updates
    {
        const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
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
        const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
            {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
            {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
            {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
            {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
             0xdeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};
    };

    namespace var_len_updates
    {
        const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
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
    class InMemoryTrie : public ::testing::Test
    {
    public:
        node_ptr root;
        UpdateAux update_aux;

        InMemoryTrie()
            : root{}
            , update_aux(std::make_unique<StateMachineWithBlockNo>(1), nullptr)
        {
        }

        monad::byte_string root_hash()
        {
            if (this->root.get()) {
                monad::byte_string res(32, 0);
                this->update_aux.comp().compute(res.data(), this->root.get());
                return res;
            }
            return empty_trie_hash;
        }

        constexpr bool on_disk() const
        {
            return update_aux.is_on_disk();
        }

        constexpr MONAD_ASYNC_NAMESPACE::storage_pool *get_storage_pool() const
        {
            return nullptr;
        }
    };
    class OnDiskTrie : public ::testing::Test
    {
    private:
        monad::async::storage_pool pool{
            monad::async::use_anonymous_inode_tag{}};
        monad::io::Ring ring;
        monad::io::Buffers rwbuf;
        MONAD_ASYNC_NAMESPACE::AsyncIO io;

    public:
        node_ptr root;
        UpdateAux update_aux;

        OnDiskTrie()
            : ring(monad::io::Ring(2, 0))
            , rwbuf(
                  ring, 2, 2,
                  MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
                  MONAD_ASYNC_NAMESPACE::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE)
            , io(pool, ring, rwbuf)
            , root{}
            , update_aux(std::make_unique<StateMachineWithBlockNo>(1), &io)
        {
        }

        monad::byte_string root_hash()
        {
            if (this->root.get()) {
                monad::byte_string res(32, 0);
                this->update_aux.comp().compute(res.data(), this->root.get());
                return res;
            }
            return empty_trie_hash;
        }

        constexpr bool on_disk() const
        {
            return update_aux.is_on_disk();
        }

        constexpr MONAD_ASYNC_NAMESPACE::storage_pool *get_storage_pool()
        {
            return &io.storage_pool();
        }
    };

    template <typename BaseTrie>
    class EraseFixture : public BaseTrie
    {
    public:
        EraseFixture()
            : BaseTrie()
        {
            auto &kv = fixed_updates::kv;

            std::vector<Update> update_vec;
            std::ranges::transform(
                kv, std::back_inserter(update_vec), [](auto &su) -> Update {
                    auto &[k, v] = su;
                    return make_update(k, v);
                });
            this->root =
                upsert_vector(this->update_aux, nullptr, std::move(update_vec));
        }
    };

}