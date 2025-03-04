/**
 * parse an evm opcode text file and return the resulting vector of evm bytecode
 *
 * Notes:
 * case is ignored
 * data can be in hex or decimal form
 * pushes can be statically sized (e.g. push3 0xabcdef) or computed size (e.g.
 * push 0xabcdef) jumpdests can use named labels, e.g. push .mylabel jumpdest
 * .mylabel end of line comments (// .. \n) and whitespace are ignored
 */

#include <CLI/CLI.hpp>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <iostream>
#include <iterator>
#include <monad/utils/parser.hpp>
#include <string>
#include <vector>

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
        if (args.verbose) {
            std::cout << "compiling " << filename << '\n';
        }
        auto in = std::ifstream(filename, std::ios::binary);
        if (args.binary) {
            auto opcodes = std::vector<uint8_t>(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());

            show_opcodes(opcodes);
        }
        else {
            auto s = std::string(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());

            auto opcodes = parse_opcodes(args.verbose, filename, s);
            auto outfile = filename + ".evm";
            if (args.verbose) {
                std::cout << "writing " << outfile << '\n';
            }
            auto out = std::ofstream(outfile, std::ios::binary);
            out.write(
                reinterpret_cast<char const *>(opcodes.data()),
                (long)opcodes.size());
            out.close();
        }
    }
    return 0;
}
