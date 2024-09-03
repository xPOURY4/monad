#include <runtime/runtime.h>

#include <evmc/evmc.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

static evmc_bytes32 from_i256_ptr(uint8_t *ptr)
{
    evmc_bytes32 result = {.bytes = {0}};
    for (int i = 0; i < 32; ++i) {
        result.bytes[31 - i] = ptr[i];
    }
    return result;
}

static bool is_zero(evmc_bytes32 *word)
{
    for (int i = 0; i < 32; ++i) {
        if (word->bytes[i] != 0) {
            return false;
        }
    }

    return true;
}

static bool equal(evmc_bytes32 *a, evmc_bytes32 *b)
{
    for (int i = 0; i < 32; ++i) {
        if (a->bytes[i] != b->bytes[i]) {
            return false;
        }
    }

    return true;
}

int64_t monad_evm_gas_left = LLONG_MAX;

void monad_evm_runtime_sstore(
    struct monad_runtime_interface *host, uint8_t *key_bytes,
    uint8_t *val_bytes)
{
    evmc_bytes32 key = from_i256_ptr(key_bytes);
    evmc_bytes32 val = from_i256_ptr(val_bytes);

    int64_t gas = 0;
    evmc_bytes32 current_val =
        host->host->get_storage(host->context, &host->message->recipient, &key);

    if (equal(&val, &current_val)) {
        gas = 100;
    }
    else {
        // Doesn't consider the original value, and assumes current == original!
        // Unsound
        if (is_zero(&current_val)) {
            gas = 20000;
        }
        else {
            gas = 2900;
        }
    }

    enum evmc_access_status status = host->host->access_storage(
        host->context, &host->message->recipient, &key);
    if (status == EVMC_ACCESS_COLD) {
        gas += 2100;
    }

    host->host->set_storage(
        host->context, &host->message->recipient, &key, &val);
    monad_evm_gas_left -= gas;
}

void monad_evm_runtime_stop(struct monad_runtime_interface *host)
{
    host->result.status_code = EVMC_SUCCESS;
    host->result.gas_left = monad_evm_gas_left;
}

void monad_evm_runtime_set_gas(int64_t value)
{
    monad_evm_gas_left = value;
}
