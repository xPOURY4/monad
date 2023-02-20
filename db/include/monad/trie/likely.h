#pragma once

#define MONAD_TRIE_LIKELY(x) __builtin_expect(!!(x), 1)
#define MONAD_TRIE_UNLIKELY(x) __builtin_expect(!!(x), 0)
