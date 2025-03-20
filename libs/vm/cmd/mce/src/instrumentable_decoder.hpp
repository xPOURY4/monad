#pragma once

#include <monad/utils/assert.h>
#include <monad/utils/load_program.hpp>

#include <valgrind/cachegrind.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

template <bool instrument>
class InstrumentableDecoder
{
public:
    std::vector<uint8_t> decode(std::string const &filename)
    {
        std::vector<char> bytes = read_file(filename);
        if constexpr (instrument) {
            CACHEGRIND_START_INSTRUMENTATION;
            std::vector<uint8_t> code = monad::utils::parse_hex_program(bytes);
            CACHEGRIND_STOP_INSTRUMENTATION;
            return code;
        }
        else {
            return monad::utils::parse_hex_program(bytes);
        }
    }

private:
    std::vector<char> read_file(std::string const &filename)
    {
        if (filename == "-") {
            std::streamsize size = 65536; // 64K
            std::vector<char> contents;
            contents.reserve(static_cast<size_t>(size));

            char c;
            while (std::cin >> c) {
                contents.push_back(c);
            }
            return contents;
        }

        std::ifstream file(filename.c_str(), std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        // Get the file size
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        MONAD_COMPILER_ASSERT(size >= 0);

        // Allocate buffer and read file contents
        std::vector<char> contents(static_cast<size_t>(size), 0);
        if (!file.read(contents.data(), size)) {
            throw std::runtime_error("Failed to read file: " + filename);
        }
        return contents;
    }
};
