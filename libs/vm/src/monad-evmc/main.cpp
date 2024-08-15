#include <compiler/compiler.h>

#include <CLI/CLI.hpp>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

std::vector<uint8_t> load_program(fs::path const &path)
{
    auto in = std::ifstream(path);
    auto hex_chars = std::vector<char>(std::istreambuf_iterator<char>(in), {});

    if (hex_chars.size() % 2 != 0) {
        throw std::runtime_error(
            "Malformed hex input (expecting an even number of hex characters)");
    }

    auto program = std::vector<uint8_t>(hex_chars.size() / 2);

    for (auto i = 0u; i < hex_chars.size(); i += 2) {
        std::from_chars(&hex_chars[i], &hex_chars[i + 1], program[i / 2], 16);
    }

    return program;
}

int main(int argc, char **argv)
{
    auto app = CLI::App{"monad-evmc"};

    auto input_path = fs::path{};
    app.add_option("-i,--input", input_path, "EVM bytecode file to compile")
        ->required()
        ->check(CLI::ExistingFile);

    auto output_path = std::optional<fs::path>{};
    app.add_option(
        "-o,--output",
        output_path,
        "Output path for compiled LLVM (defaults to stdout)");

    CLI11_PARSE(app, argc, argv);

    auto program_bytes = load_program(input_path);

    auto [mod, _] = monad::compiler::compile_evm_bytecode(
        program_bytes.data(), program_bytes.size());

    if (output_path.has_value()) {
        auto err = std::error_code{};
        auto os = llvm::raw_fd_ostream(
            output_path->string(), err, llvm::sys::fs::FA_Write);
        os << *mod << '\n';
    }
    else {
        llvm::outs() << *mod << '\n';
    }

    return 0;
}
