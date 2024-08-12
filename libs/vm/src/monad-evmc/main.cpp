#include <compiler/compiler.h>

#include <CLI/CLI.hpp>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

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
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
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
