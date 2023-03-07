#include <monad/trie/data.h>
#include <stdlib.h>

void copy_trie_data(trie_data_t *const dest, trie_data_t const *const src)
{
    for (unsigned i = 0; i < 4; ++i) {
        dest->words[i] = src->words[i];
    }
}

bool cmp_trie_data(trie_data_t *const dest, trie_data_t const *const src)
{ // 0: identical, 1: diff

    for (unsigned i = 0; i < 4; ++i) {
        if (dest->words[i] != src->words[i]) {
            return true;
        }
    }
    return false;
}