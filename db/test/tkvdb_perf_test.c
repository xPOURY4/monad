#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <alloca.h>
#include <time.h>
#include <signal.h>

#include "tkvdb.h"
#include <ethash/keccak.h>
#include "string.h"

/* transaction size in bytes */
unsigned long long trsize = 1024*1024*1024*50LL;
/* magic numbers */
static size_t one_m = 1000000;


static void
ctrl_c_handler(int s)
{
    (void)s;
    exit(0);
}

/*  Commit one batch of updates
    offset: key offset, insert key starting from this number
    nkeys: number of keys to insert in this batch
 */
static void batch_commit(tkvdb_tr* tr, uint64_t offset, uint64_t nkeys){
    tkvdb_datum dtk, dtv;
    struct timespec ts_before, ts_after;
    size_t keylen = 32, vallen = keylen;
    double tm_ram, tm_commit;
    size_t i, rand_val;
    union ethash_hash256 hash;
    
    dtk.size = keylen;
    dtv.size = vallen;

    // pre-calculate keccak
    char* keccak_keys = malloc(nkeys * 32);
    char* keccak_values = malloc(nkeys * 32);
    // populate the trie with 100M entries on-disk
    for(i=offset; i<offset+nkeys; i++){
        // assign keccak256 on i to key
        hash = ethash_keccak256((const uint8_t *)&i, 32);
        memcpy(keccak_keys + (i-offset)*32, hash.str, 32);

        rand_val = rand();
        hash = ethash_keccak256((const uint8_t *)&rand_val, 32);
        memcpy(keccak_values + (i-offset)*32, hash.str, 32);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    // populate the trie with 100M entries on-disk
    for(i=0; i<nkeys; i++){
        dtk.data = keccak_keys+i*32;
        dtv.data = keccak_values+i*32;
        // put to tr
        assert(tr->put(tr, &dtk, &dtv) == TKVDB_OK);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_ram = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9)
        - ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);
    // commit the tr to on-disk storage
    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    tr->commit(tr);
    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    tm_commit = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9)
        - ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);

    printf("total_keys_in_db: %lu, nkeys: %lu, insert in RAM: %f /s, commit_t: %f s\n", offset+nkeys, nkeys, (double)nkeys/tm_ram, tm_commit);
}


int main(){
    struct sigaction sig;
    sig.sa_handler = &ctrl_c_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGINT, &sig, NULL);

    // create tr
    tkvdb_params *params;
    tkvdb* db;
    tkvdb_tr *tr;

    params = tkvdb_params_create();
    assert(params);
    tkvdb_param_set(params, TKVDB_PARAM_TR_DYNALLOC, 0);
    tkvdb_param_set(params, TKVDB_PARAM_TR_LIMIT, trsize);
    db = tkvdb_open("keccak_test_db.tkvdb", params);
    tr = tkvdb_tr_create(db, params);
    assert(tr);
    tkvdb_params_free(params);

    /* Stage 1. build 100M trie on disk */
    for(uint8_t iter = 0; iter < 100; iter++){
        assert(tr->begin(tr) == TKVDB_OK);
        batch_commit(tr, iter * one_m, one_m);
        // tr->free(tr); // seg fault, need to look into the implementation
    }

    /* Stage 2. 2M new batch updates */
    assert(tr->begin(tr) == TKVDB_OK);
    batch_commit(tr, 100*one_m, 2*one_m); 
    tkvdb_close(db);
}
