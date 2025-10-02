// Copyright (C) 2025 Category Labs, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <category/execution/ethereum/chain/chain_config.h>
#include <category/execution/ethereum/trace/tracer_config.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

static uint64_t const MONAD_ETH_CALL_LOW_GAS_LIMIT = 400'000;

struct monad_state_override;
struct monad_eth_call_executor;

struct monad_state_override *monad_state_override_create();

void monad_state_override_destroy(struct monad_state_override *);

void add_override_address(
    struct monad_state_override *, uint8_t const *addr, size_t addr_len);

void set_override_balance(
    struct monad_state_override *, uint8_t const *addr, size_t addr_len,
    uint8_t const *balance, size_t balance_len);

void set_override_nonce(
    struct monad_state_override *, uint8_t const *addr, size_t addr_len,
    uint64_t nonce);

void set_override_code(
    struct monad_state_override *, uint8_t const *addr, size_t addr_len,
    uint8_t const *code, size_t code_len);

void set_override_state_diff(
    struct monad_state_override *, uint8_t const *addr, size_t addr_len,
    uint8_t const *key, size_t key_len, uint8_t const *value,
    size_t valuen_len);

void set_override_state(
    struct monad_state_override *, uint8_t const *addr, size_t addr_len,
    uint8_t const *key, size_t key_len, uint8_t const *value,
    size_t valuen_len);

typedef struct monad_eth_call_result
{
    int status_code;
    int64_t gas_used;
    int64_t gas_refund;

    uint8_t *output_data;
    size_t output_data_len;

    char *message;

    // for trace (call, prestate, statediff)
    uint8_t *encoded_trace;
    size_t encoded_trace_len;
} monad_eth_call_result;

void monad_eth_call_result_release(monad_eth_call_result *);

struct monad_eth_call_pool_config
{
    // Number of threads in the pool.
    unsigned num_threads;

    // Number of fibers per thread.
    unsigned num_fibers;

    // Timeout request if it failed to be scheduled in this time.
    unsigned timeout_sec;

    // Maximum number of requests in the queue. Request is removed from the
    // queue when it starts executing.
    unsigned queue_limit;
};

struct monad_eth_call_executor *monad_eth_call_executor_create(
    struct monad_eth_call_pool_config low_pool_conf,
    struct monad_eth_call_pool_config high_pool_conf, uint64_t node_lru_max_mem,
    char const *dbpath);

void monad_eth_call_executor_destroy(struct monad_eth_call_executor *);

void monad_eth_call_executor_submit(
    struct monad_eth_call_executor *, enum monad_chain_config,
    uint8_t const *rlp_txn, size_t rlp_txn_len, uint8_t const *rlp_header,
    size_t rlp_header_len, uint8_t const *rlp_sender, size_t rlp_sender_len,
    uint64_t block_number, uint8_t const *rlp_block_id, size_t rlp_block_id_len,
    struct monad_state_override const *,
    void (*complete)(monad_eth_call_result *, void *user), void *user,
    enum monad_tracer_config, bool gas_specified);

#ifdef __cplusplus
}
#endif
