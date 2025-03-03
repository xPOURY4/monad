#pragma once

#include <charconv>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace monad::utils
{
char const *try_parse_line_comment(char const *input);

    /**
     * parse an evm opcode text file and return the resulting vector of evm bytecode
     *
     * Notes:
     * case is ignored
     * data can be in hex or decimal form
     * pushes can be statically sized (e.g. push3 0xabcdef) or computed size (e.g. push 0xabcdef)
     * jumpdests can use named labels, e.g. push .mylabel jumpdest .mylabel
     * end of line comments (// .. \n) and whitespace are ignored
     */

std::vector<uint8_t> parse_opcodes(bool verbose, std::string filename, std::string str);

    /**
     * show text opcodes and data from binary evm bytecode
     */
void show_opcodes(std::vector<uint8_t> &opcodes);

}
