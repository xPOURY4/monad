#pragma once

#include <cassert>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <vector>

namespace monad::utils
{

    template <typename It>
    std::vector<uint8_t> parse_hex_program(It begin, It end)
    {
        auto hex_size = std::distance(begin, end);
        auto program = std::vector<uint8_t>(hex_size / 2);

        auto out_begin = program.begin();
        auto out_end = program.end();

        for (auto input_it = begin, output_it = out_begin;
             input_it != end && output_it != out_end;
             input_it += 2, output_it++) {
            std::from_chars(&*input_it, (&*input_it) + 2, *output_it, 16);
        }

        return program;
    }

    template <typename Container>
    std::vector<uint8_t> parse_hex_program(Container const &c)
    {
        using std::begin;
        using std::end;
        return parse_hex_program(begin(c), end(c));
    }

}
