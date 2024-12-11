#include "blockchaintest.hpp"

#include <test_resource_data.h>

#include <vm/vm.h>

#include <evmc/evmc.hpp>

using namespace evmone::test;

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    auto const root = test_resource::ethereum_tests_dir / "BlockchainTests";

    auto vm = evmc::VM{evmc_create_monad_compiler_vm()};
    blockchain_test_setup(&argc, argv);
    return blockchain_test_main({root}, false, vm);
}
