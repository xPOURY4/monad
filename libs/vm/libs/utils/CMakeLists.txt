enable_language(C)

add_library(utils STATIC)

target_sources(utils PRIVATE
    "src/monad/vm/utils/cases.hpp"
    "src/monad/vm/utils/debug.cpp"
    "src/monad/vm/utils/debug.hpp"
    "src/monad/vm/utils/load_program.hpp"
    "src/monad/vm/utils/rc_ptr.hpp"
    "src/monad/vm/utils/uint256.cpp"
    "src/monad/vm/utils/uint256.hpp"
    "src/monad/vm/utils/parser.cpp"
    "src/monad/vm/utils/parser.hpp"
)

monad_compile_options(utils)

target_include_directories(utils
    PUBLIC src/
)

target_link_libraries(utils
    PUBLIC intx::intx
    PUBLIC monad-evm
    PUBLIC monad-vm::monad-vm-core
)
