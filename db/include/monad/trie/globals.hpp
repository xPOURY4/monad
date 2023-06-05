#pragma once

#include <boost/pool/object_pool.hpp>
#include <monad/trie/config.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

struct Request;

// cpool for all objects with single version lifetime
extern cpool_29_t *tmppool_;
// object pools
extern boost::object_pool<Request> request_pool;

MONAD_TRIE_NAMESPACE_END