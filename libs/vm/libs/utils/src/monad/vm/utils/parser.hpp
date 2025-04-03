#pragma once

#include <charconv>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace monad::vm::utils
{
    /**
     * parse an evm opcode string and
     * return the resulting vector of evm bytecode
     *
     * Notes:
     * case is ignored
     * data can be in hex or decimal form
     * pushes can be statically sized (e.g. push3 0xabcdef)
     * or computed size (e.g. push 0xabcdef)
     * jumpdests can use named labels,
     * e.g. push .mylabel jumpdest .mylabel
     * end of line comments (// .. \n) and whitespace are ignored
     *
     */

    std::vector<uint8_t> parse_opcodes(std::string const &str);

    /**
     * the same as parse_opcodes but it writes info to stderr as it is working
     */
    std::vector<uint8_t> parse_opcodes_verbose(std::string const &str);

    /**
     *  convert from binary evm bytecode to text opcodes and data
     */
    std::string show_opcodes(std::vector<uint8_t> const &opcodes);

}
