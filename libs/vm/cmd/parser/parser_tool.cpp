/**
 * parse an evm opcode text <file> and
 * write the resulting evm bytecode to <file>.evm
 *
 * -b switch reads in an evm bytecode file and
 *  writes the corresponding text to stdout
 *
 * see parser.hpp for details
 *
 */

#include <cstdint>
#include <cstdlib>
#include <ios>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include <monad/utils/parser.hpp>

using namespace monad::utils;

struct arguments
{
    bool verbose = false;
    bool binary = false;
    std::vector<std::string> filenames;
};

arguments parse_args(int const argc, char **const argv)
{
    auto app = CLI::App("evm opcode parser/printer");
    auto args = arguments{};

    app.add_option("filenames", args.filenames, "List of files to process")
        ->required()
        ->check(CLI::ExistingFile);

    app.add_flag(
        "-b,--binary",
        args.binary,
        "process input files as binary and show evm opcodes/data as text");

    app.add_flag("-v,--verbose", args.verbose, "send debug info to stdout");

    try {
        app.parse(argc, argv);
    }
    catch (CLI::ParseError const &e) {
        std::exit(app.exit(e));
    }

    return args;
}

int main(int argc, char **argv)
{
    auto args = parse_args(argc, argv);
    for (auto const &filename : args.filenames) {
        auto in = std::ifstream(filename, std::ios::binary);
        if (args.binary) {
            if (args.verbose) {
                std::cerr << "printing " << filename << '\n';
            }
            auto opcodes = std::vector<uint8_t>(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());

            std::cout << show_opcodes(opcodes) << '\n';
        }
        else {
            if (args.verbose) {
                std::cerr << "parsing " << filename << '\n';
            }
            auto s = std::string(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());

            auto opcodes =
                args.verbose ? parse_opcodes_verbose(s) : parse_opcodes(s);

            auto outfile = filename + ".evm";
            if (args.verbose) {
                std::cerr << "writing " << outfile << '\n';
            }
            auto out = std::ofstream(outfile, std::ios::binary);
            out.write(
                reinterpret_cast<char const *>(opcodes.data()),
                static_cast<long>(opcodes.size()));
        }
    }
    return 0;
}
