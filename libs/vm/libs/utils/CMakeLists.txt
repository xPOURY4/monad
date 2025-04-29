enable_language(C)

add_library(monad-vm-utils STATIC)

target_sources(monad-vm-utils PRIVATE
    "src/monad/vm/utils/cached_allocator.hpp"
    "src/monad/vm/utils/cases.hpp"
    "src/monad/vm/utils/debug.cpp"
    "src/monad/vm/utils/debug.hpp"
    "src/monad/vm/utils/load_program.hpp"
    "src/monad/vm/utils/rc_ptr.hpp"
    "src/monad/vm/utils/uint256.cpp"
    "src/monad/vm/utils/uint256.hpp"
    "src/monad/vm/utils/parser.cpp"
    "src/monad/vm/utils/parser.hpp"
    "src/monad/vm/utils/scope_exit.hpp"
    "src/monad/vm/utils/traits.hpp"
)

monad_compile_options(monad-vm-utils)

target_include_directories(monad-vm-utils
    PUBLIC src/
)

target_link_libraries(monad-vm-utils
    PUBLIC intx::intx
    PUBLIC monad-vm::monad-vm-core
    PRIVATE monad-vm::monad-vm-evm
)

add_library(monad-vm::monad-vm-utils ALIAS monad-vm-utils)