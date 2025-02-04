add_library(utils STATIC)

target_sources(utils PRIVATE
    "src/monad/utils/assert.c"
    "src/monad/utils/assert.hpp"
    "src/monad/utils/load_program.hpp"
    "src/monad/utils/rc_ptr.hpp"
    "src/monad/utils/uint256.cpp"
    "src/monad/utils/uint256.hpp"
)

monad_compile_options(utils)

target_include_directories(utils
    PUBLIC src/
)

target_link_libraries(utils
    PUBLIC intx::intx
)