add_library(utils STATIC
    src/assert.c
    src/uint256.cpp
)
monad_compile_options(utils)
target_include_directories(utils PUBLIC include/)
target_link_libraries(utils
    PUBLIC intx::intx
)