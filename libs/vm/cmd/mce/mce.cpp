#include <CLI/CLI.hpp>
#include <asmjit/core/jitruntime.h>
#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <instrumentable_compiler.hpp>
#include <instrumentable_decoder.hpp>
#include <instrumentable_parser.hpp>
#include <instrumentable_vm.hpp>
#include <monad/vm/compiler/ir/basic_blocks.hpp>
#include <monad/vm/compiler/ir/x86/types.hpp>
#include <monad/vm/compiler/types.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <ios>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

struct arguments
{
    std::string filename;
    std::string revision = "latest";
    bool instrument_decode = false;
    bool instrument_parse = false;
    bool instrument_compile = false;
    bool instrument_execute = false;
    std::optional<std::string> asm_log_file;
};

static arguments parse_args(int const argc, char **const argv)
{
    auto app =
        CLI::App("Instrumentable standalone monad compiler execution engine");
    auto args = arguments{};

    app.add_option("filename", args.filename, "File to compile and execute");
    app.add_option(
        "--rev",
        args.revision,
        std::format("Set EVM revision (default: {})", args.revision));
    app.add_flag(
        "-d",
        args.instrument_decode,
        std::format(
            "Instrument decoding (default: {})", args.instrument_decode));
    app.add_flag(
        "-p",
        args.instrument_parse,
        std::format("Instrument parsing (default: {})", args.instrument_parse));
    app.add_flag(
        "-c",
        args.instrument_compile,
        std::format(
            "Instrument compilation (default: {})", args.instrument_compile));
    app.add_flag(
        "-e",
        args.instrument_execute,
        std::format(
            "Instrument execution (default: {})", args.instrument_execute));
    app.add_option(
        "--log-asm",
        args.asm_log_file,
        std::format("Log assembly output to file"));

    try {
        app.parse(argc, argv);
        if (args.filename.empty()) {
            throw CLI::ParseError{"filename: no input file", 105};
        }
    }
    catch (CLI::ParseError const &e) {
        std::exit(app.exit(e));
    }

    return args;
}

static void dump_result(evmc::Result const &result)
{
    if (result.status_code == EVMC_SUCCESS) {
        if (result.output_size == 0) {
            return;
        }
        uint256_t const x = uint256_t::load_be_unsafe(&result.output_data[0]);
        std::cout << x.to_string(16) << std::endl;
        return;
    }
    std::cerr << "fatal error: ";
    switch (result.status_code) {
    case EVMC_FAILURE:
        std::cerr << "failure";
        break;
    case EVMC_INTERNAL_ERROR:
        std::cerr << "internal error";
        break;
    case EVMC_OUT_OF_GAS:
        std::cerr << "out of gas";
        break;
    case EVMC_STACK_OVERFLOW:
        std::cerr << "stack overflow";
        break;
    case EVMC_STACK_UNDERFLOW:
        std::cerr << "stack underflow";
        break;
    default:
        std::cerr << "unknown failure";
    }
}

template <evmc_revision Rev>
int mce_main(arguments const &args)
{
    std::vector<uint8_t> const bytes = [&]() {
        if (args.instrument_decode) {
            InstrumentableDecoder<true> decoder{};
            return decoder.decode(args.filename);
        }
        else {
            InstrumentableDecoder<false> decoder{};
            return decoder.decode(args.filename);
        }
    }();

    std::optional<monad::vm::compiler::basic_blocks::BasicBlocksIR> const ir =
        [&]() {
            if (args.instrument_parse) {
                InstrumentableParser<true> parser{};
                return parser.parse<Rev>(bytes);
            }
            else {
                InstrumentableParser<false> parser{};
                return parser.parse<Rev>(bytes);
            }
        }();
    if (!ir) {
        std::cerr << "Parsing failed" << std::endl;
        return 1;
    }

    asmjit::JitRuntime rt{};
    monad::vm::compiler::native::CompilerConfig config{};
    if (args.asm_log_file) {
        config.asm_log_path = args.asm_log_file->c_str();
    }
    std::shared_ptr<native::Nativecode> const ncode = [&]() {
        if (args.instrument_compile) {
            InstrumentableCompiler<true> compiler(rt, config);
            return compiler.compile(Rev, *ir);
        }
        else {
            InstrumentableCompiler<false> compiler(rt, config);
            return compiler.compile(Rev, *ir);
        }
    }();

    if (!ncode->entrypoint()) {
        std::cerr << "Compilation failed" << std::endl;
        return 1;
    }

    evmc::Result const result = [&]() {
        if (args.instrument_execute) {
            InstrumentableVM<true> vm(rt);
            return vm.execute(Rev, ncode->entrypoint());
        }
        else {
            InstrumentableVM<false> vm(rt);
            return vm.execute(Rev, ncode->entrypoint());
        }
    }();

    dump_result(result);

    auto status_code = result.status_code;

    return status_code == EVMC_SUCCESS ? 0 : 1;
}

static std::string uppercase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
    return s;
}

int main(int argc, char **argv)
{
    std::ios_base::sync_with_stdio(false);
    auto args = parse_args(argc, argv);
    std::string const rev = uppercase(args.revision);
    if (rev == "FRONTIER") {
        return mce_main<EVMC_FRONTIER>(args);
    }
    else if (rev == "HOMESTEAD") {
        return mce_main<EVMC_HOMESTEAD>(args);
    }
    else if (rev == "TANGERINE_WHISTLE") {
        return mce_main<EVMC_TANGERINE_WHISTLE>(args);
    }
    else if (rev == "SPURIOUS_DRAGON") {
        return mce_main<EVMC_SPURIOUS_DRAGON>(args);
    }
    else if (rev == "BYZANTIUM") {
        return mce_main<EVMC_BYZANTIUM>(args);
    }
    else if (rev == "CONSTANTINOPLE") {
        return mce_main<EVMC_CONSTANTINOPLE>(args);
    }
    else if (rev == "PETERSBURG") {
        return mce_main<EVMC_PETERSBURG>(args);
    }
    else if (rev == "ISTANBUL") {
        return mce_main<EVMC_ISTANBUL>(args);
    }
    else if (rev == "BERLIN") {
        return mce_main<EVMC_BERLIN>(args);
    }
    else if (rev == "LONDON") {
        return mce_main<EVMC_LONDON>(args);
    }
    else if (rev == "PARIS") {
        return mce_main<EVMC_PARIS>(args);
    }
    else if (rev == "SHANGHAI") {
        return mce_main<EVMC_SHANGHAI>(args);
    }
    else if (rev == "CANCUN") {
        return mce_main<EVMC_CANCUN>(args);
    }
    else if (rev == "LATEST") {
        return mce_main<EVMC_LATEST_STABLE_REVISION>(args);
    }
    else {
        std::cerr << std::format(
                         "error: unsupported revision '{}'", args.revision)
                  << std::endl;
        return 1;
    }
}
