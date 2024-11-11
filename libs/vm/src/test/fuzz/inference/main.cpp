#include <compiler/compiler.h>
#include <utils/load_program.h>

#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/ir/local_stacks.h>
#include <compiler/ir/poly_typed.h>
#include <compiler/ir/poly_typed/block.h>
#include <compiler/ir/poly_typed/infer.h>
#include <compiler/ir/poly_typed/kind.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <vector>

__AFL_FUZZ_INIT();

#pragma clang diagnostic ignored "-Weverything"
#pragma clang optimize off

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    __AFL_INIT();

#ifdef AFL_PERSISTENT_REPLAY_ARGPARSE
    char const *filename = argv[1];
    auto in = std::ifstream(filename, std::ios::binary);
    auto program = std::vector<uint8_t>(
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
#else
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(100000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        auto program = std::vector(buf, buf + len);
#endif
    auto bytecode = monad::compiler::bytecode::BytecodeIR(program);
    auto ir = monad::compiler::poly_typed::PolyTypedIR(
        monad::compiler::local_stacks::LocalStacksIR(
            monad::compiler::basic_blocks::BasicBlocksIR(bytecode)));
    ir.type_check_or_throw();
#ifndef AFL_PERSISTENT_REPLAY_ARGPARSE
}
#endif
}
