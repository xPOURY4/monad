#include "fixture.h"

#include <runtime/transmute.h>

#include <evmc/evmc.hpp>

#include <intx/intx.hpp>

#include <cstdint>
#include <limits>

using namespace monad::runtime;

using namespace intx;

namespace monad::compiler::test
{
    RuntimeTest::RuntimeTest()
    {
        ctx_ = Context{
            .host = &host_.get_interface(),
            .context = host_.to_context(),
            .gas_remaining = std::numeric_limits<std::int64_t>::max(),
            .gas_refund = 0,
            .env =
                {
                    .evmc_flags = 0,
                    .depth = 0,
                    .recipient =
                        0x0000000000000000000000000000000000000001_address,
                    .sender =
                        0x0000000000000000000000000000000000000002_address,
                    .value = {},
                    .create2_salt = {},
                    .input_data = {},
                    .code = {},
                    .return_data = {},
                },
        };

        host_.tx_context = evmc_tx_context{
            .tx_gas_price = bytes_from_uint256(56762),
            .tx_origin = 0x000000000000000000000000000000005CA1AB1E_address,
            .block_coinbase =
                0x00000000000000000000000000000000BA5EBA11_address,
            .block_number = 23784,
            .block_timestamp = 1733494490,
            .block_gas_limit = 30000000,
            .block_prev_randao = bytes_from_uint256(89273),
            .chain_id = bytes_from_uint256(2342),
            .block_base_fee = bytes_from_uint256(389),
            .blob_base_fee = bytes_from_uint256(98988),
            .blob_hashes = nullptr,
            .blob_hashes_count = 0,
            .initcodes = nullptr,
            .initcodes_count = 0,
        };

        host_.block_hash = bytes_from_uint256(
            0x105DF6064F84551C4100A368056B8AF0E491077245DAB1536D2CFA6AB78421CE_u256);
    }
}
