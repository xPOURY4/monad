#include <monad/trie/data.h>
#include <stdlib.h>

void copy_trie_data(trie_data_t *dest_data, trie_data_t *src_data)
{
    for (unsigned char i = 0; i < 4; i++) {
        dest_data->words[i] = src_data->words[i];
    }
}

bool cmp_trie_data(trie_data_t *dest_data, trie_data_t *src_data)
{ // 0: identical, 1: diff

    for (unsigned char i = 0; i < 4; i++) {
        if (dest_data->words[i] != src_data->words[i]) {
            return true;
        }
    }
    return false;
}