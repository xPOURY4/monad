#pragma once

#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/util.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute;
struct Node;

Node *upsert(Compute &comp, Node *const old, UpdateList &&updates);

MONAD_MPT_NAMESPACE_END