#pragma once

#include <monad/mpt/config.hpp>

#include <functional>

MONAD_MPT_NAMESPACE_BEGIN

class Node;

struct TraverseMachine
{
    virtual void down(Node const &) = 0;
    virtual void up(Node const &) = 0;
};

void preorder_traverse(Node const &, TraverseMachine &);

MONAD_MPT_NAMESPACE_END
