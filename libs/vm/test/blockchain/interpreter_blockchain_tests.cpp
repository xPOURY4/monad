#include "test_vm.hpp"

#include <test_resource_data.h>

#include <blockchaintest.hpp>

#include <evmc/evmc.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace monad;
using namespace monad::compiler;

using namespace evmc::literals;
using namespace evmone::test;

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    auto const root = test_resource::ethereum_tests_dir / "BlockchainTests";

    auto vm = evmc::VM{
        new BlockchainTestVM(BlockchainTestVM::Implementation::Interpreter)};
    blockchain_test_setup(&argc, argv);
    // Skip slow tests:
    testing::FLAGS_gtest_filter +=
        ":-*" // TODO(BSC): actually run tests
        ":-"
        "GeneralStateTests/VMTests/vmPerformance.loopExp:"
        "GeneralStateTests/VMTests/vmPerformance.loopMul:"
        "GeneralStateTests/stTimeConsuming.CALLBlake2f_MaxRounds:"
        "GeneralStateTests/stTimeConsuming.static_Call50000_sha256";
    return blockchain_test_main({root}, false, vm);
}
