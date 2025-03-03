#include <CLI/CLI.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <evmc/evmc.h>
#include <intx/intx.hpp>
#include <iostream>
#include <monad/evm/opcodes.hpp>
#include <monad/utils/cases.hpp>
#include <monad/utils/parser.hpp>
#include <monad/utils/uint256.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace monad::compiler;

namespace monad::utils
{

constexpr auto push_ops = std::array{
    "PUSH", // generic push
    "PUSH0",  "PUSH1",  "PUSH2",  "PUSH3",  "PUSH4",  "PUSH5",  "PUSH6",
    "PUSH7",  "PUSH8",  "PUSH9",  "PUSH10", "PUSH11", "PUSH12", "PUSH13",
    "PUSH14", "PUSH15", "PUSH16", "PUSH17", "PUSH18", "PUSH19", "PUSH20",
    "PUSH21", "PUSH22", "PUSH23", "PUSH24", "PUSH25", "PUSH26", "PUSH27",
    "PUSH28", "PUSH29", "PUSH30", "PUSH31", "PUSH32",
};

char const *try_parse_line_comment(char const *input)
{
    if (*input == '/') {
        do {
            input++;
        }
        while (*input && *input != '\n');
    }
    return input;
}

char const *try_parse_hex_constant(char const *input)
{
    auto const *input0 = input;

    if (*input == '0') {
        input++;
        if (*input == 'x' || *input == 'X') {
            input++;
            if (isxdigit(*input)) {
                do {
                    input++;
                }
                while (isxdigit(*input));
                return input;
            }
        }
    }
    return input0;
}

char const *try_parse_decimal_constant(char const *input)
{
    while (isdigit(*input)) {
        input++;
    }
    return input;
}

char const *try_parse_label(char const *input)
{
    if (*input == '.') {
        do {
            input++;
        }
        while (isalnum(*input));
    }
    return input;
}

char const *drop_spaces(char const *input)
{
    while (*input == ' ') {
        input++;
    }
    return input;
}

void err(std::string msg)
{
    std::cerr << "error: " << msg << '\n';
    exit(1);
}

std::pair<char const *, std::variant<uint256_t, std::string>>
parse_constant_or_label(char const *input)

{
    input = drop_spaces(input);
    auto const *p = try_parse_hex_constant(input);
    if (p != input) {
        auto s = std::string(input, p);
        return std::make_pair(p, intx::from_string<uint256_t>(s.c_str()));
    }

    p = try_parse_decimal_constant(input);
    if (p != input) {
        auto s = std::string(input, p);
        return std::make_pair(p, intx::from_string<uint256_t>(s.c_str()));
    }
    p = try_parse_label(input);
    if (p == input) {
        err("missing argument to push");
    }

    auto s = std::string(input, p);
    return std::make_pair(p, s);
}

char const *try_parse_opname(char const *input)
{
    if (isalpha(*input)) {
        do {
            input++;
        }
        while (isalnum(*input));
    }
    return input;
}

bool is_push(std::string op)
{
    return (find(push_ops.begin(), push_ops.end(), op) != push_ops.end());
}

struct OpName
{
    std::string opname;
};

struct Push
{
    std::string opname;
    std::variant<uint256_t, std::string> arg;
};

struct JumpDest
{
    std::optional<std::string> label;
};

using Token = std::variant<Push, JumpDest, OpName>;

std::optional<uint8_t> find_opcode(std::string const op)
{
    auto tbl = monad::compiler::make_opcode_table<EVMC_CANCUN>();
    size_t i;
    for (i = 0; i < tbl.size(); ++i) {
        if (tbl[i].name == op) {
            break;
        }
    }
    if (i < tbl.size()) {
        return (uint8_t)i;
    }
    return std::nullopt;
}

std::optional<uint256_t> try_to_resolve_push_arg(
    std::variant<uint256_t, std::string> const &arg,
    std::unordered_map<std::string, std::size_t> const &known_labels)
{
    return std::visit(
        monad::utils::Cases{
            [&](uint256_t const &c) {
                return std::make_optional(c);
            },
            [&](std::string const &lbl) {
                auto search = known_labels.find(lbl);
                if (search != known_labels.end()) {
                    return std::make_optional((uint256_t)search->second);
                }
                return (std::optional<uint256_t>)std::nullopt;
            },
        },
        arg);
}

void warn(std::string msg, std::string value)
{
    std::cerr << "warning: " << msg << " " << value << '\n';
}

uint8_t min_bytes_to_store(uint256_t x)
{
    uint8_t n = 0;
    while (x > 0) {
        x >>= 8;
        n++;
    }
    return n;
}

void show_byte_at(bool verbose,
    std::vector<uint8_t> const &opcodes, std::size_t i, std::string s)
{
    if (!verbose) return;
    std::cout << "[0x" << std::hex << i << "] ";
    std::cout << " 0x" << std::hex << (int)opcodes[i];
    std::cout << s;
    std::cout << '\n';
}

void write_n_bytes_at(bool verbose,
    std::vector<uint8_t> &opcodes, uint256_t value, std::size_t n, std::size_t idx)
{
    auto sz = idx + n;
    if (sz > opcodes.size()) {
        opcodes.resize(sz);
    }

    for (auto i = idx + n - 1; i >= idx; --i) {
        opcodes[i] = (uint8_t)value & 0xff;
        value >>= 8;
    }
    for (auto i = idx; i < idx + n; ++i) {
        show_byte_at(verbose, opcodes, i, "");
    }
    if (value > 0) {
        err("value too large for push");
    }
}

int write_opcode(bool verbose, std::vector<uint8_t> &opcodes, std::string opname)
{
    auto c = find_opcode(opname);
    if (!c.has_value()) {
        warn("unknown opcode", opname);
        return -1;
    }
    opcodes.push_back(c.value());
    show_byte_at(verbose, opcodes, opcodes.size() - 1, "//     " + opname);
    return c.value();
}

void show_opcodes(std::vector<uint8_t> &opcodes)
{
    auto tbl = monad::compiler::make_opcode_table<EVMC_CANCUN>();
    for (std::size_t i = 0; i < opcodes.size(); ++i) {
        std::cout << std::hex << "[0x" << i << "] ";
        auto c = (int)opcodes[i];
        std::cout << "0x" << std::hex << c;
        std::cout << " " << tbl[c].name;
        std::cout << '\n';
        if (c >= PUSH1 && c <= PUSH32) {
            for (auto j = 0; j < c - PUSH0; ++j) {
                i++;
                std::cout << std::hex << "[0x" << i << "] ";
                std::cout << "0x" << std::hex << (int)opcodes[i] << '\n';
            }
        }
    }
}

std::vector<uint8_t> compile_tokens(bool verbose, std::vector<Token> &tokens)
{
    std::unordered_map<std::string, std::size_t>
        known_labels{}; // mapping from label to offset
    std::unordered_map<std::string, std::vector<std::size_t>>
        unknown_labels{}; // mapping from label to push references

    std::vector<uint8_t> opcodes{};

    for (auto const &tok : tokens) {
        std::visit(
            monad::utils::Cases{
                [&](OpName const &op) { write_opcode(verbose, opcodes, op.opname); },
                [&](Push const &op) {
                    auto opname = op.opname;
                    auto r = try_to_resolve_push_arg(op.arg, known_labels);
                    uint256_t value = 0xff;
                    if (!r.has_value()) // argument is unresolved label
                    {
                        auto lbl = std::get<std::string>(op.arg);
                        auto offset = opcodes.size() + 1;
                        unknown_labels[lbl].push_back(offset);
                    }
                    else {
                        value = r.value();
                    }
                    if (op.opname == "PUSH") {
                        auto n = (int)min_bytes_to_store(value);

                        opname = "PUSH" + std::to_string(n);
                    }

                    auto c = write_opcode(verbose, opcodes, opname);
                    auto nbytes = c - PUSH0;

                    write_n_bytes_at(verbose, opcodes, value, nbytes, opcodes.size());
                },
                [&](JumpDest const &op) {
                    if (op.label.has_value()) {
                        auto search = known_labels.find(op.label.value());
                        if (search != known_labels.end()) {
                            err("multiply defined label");
                        }
                        known_labels[op.label.value()] = opcodes.size();
                    }
                    write_opcode(verbose, opcodes, "JUMPDEST");
                },
            },
            tok);
    }
    // resolve labels
    if (verbose) std::cout << "// resolving labels\n";
    for (auto const &p : unknown_labels) {
        auto search = known_labels.find(p.first);
        if (search == known_labels.end()) {
            err("undefined label");
        }
        auto value = search->second;
        for (auto const &ix : p.second) {
            write_n_bytes_at(verbose, opcodes, value, 1, ix);
        }
    }
    if (verbose) std::cout << "// done\n";
    if (verbose) show_opcodes(opcodes);
    return opcodes;
}

std::vector<uint8_t> parse_opcodes(bool verbose, std::string filename, std::string str)
{
    auto tokens = std::vector<Token>{};

    char const *input = str.c_str();

    if (verbose) std::cout << "parsing " << filename << '\n';

    while (*input) {
        auto const *p = try_parse_hex_constant(input);
        if (p != input) {
            warn("unexpected hex constant", std::string(input, p));
            input = p;
            continue;
        }

        p = try_parse_decimal_constant(input);
        if (p != input) {
            warn("unexpected decimal constant", std::string(input, p));
            input = p;
            continue;
        }

        p = try_parse_label(input);
        if (p != input) {
            warn("unexpected label", std::string(input, p));
            input = p;
            continue;
        }

        p = try_parse_line_comment(input);
        if (p != input) {
            input = p;
            continue;
        }

        p = try_parse_opname(input);
        if (p != input) {
            auto op = std::string(input, p);
            std::transform(op.begin(), op.end(), op.begin(), ::toupper);
            input = p;
            if (is_push(op)) {
                auto r = parse_constant_or_label(input);
                input = r.first;
                tokens.push_back(Push{.opname = op, .arg = r.second});
            }
            else if (op == "JUMPDEST") {
                input = drop_spaces(input);
                p = try_parse_label(input);
                if (p == input) {
                    tokens.push_back(JumpDest{});
                }
                else {
                    tokens.push_back(JumpDest{.label = std::string(input, p)});
                    input = p;
                }
            }
            else {
                tokens.push_back(OpName{.opname = op});
            }

            continue;
        }

        input++; // otherwise ignore
    }
    return compile_tokens(verbose, tokens);
}

}
