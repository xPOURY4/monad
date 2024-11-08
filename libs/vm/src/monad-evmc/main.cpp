#include <compiler/compiler.h>
#include <utils/load_program.h>

#include <CLI/CLI.hpp>

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
    return monad::utils::parse_hex_program(hex_chars);
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
    (void)program_bytes;

    return 0;
}
