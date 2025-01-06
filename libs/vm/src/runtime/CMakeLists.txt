enable_language(C ASM)
add_library(runtime STATIC
    exit.S
    exit.cpp)

monad_compile_options(runtime)

target_link_libraries(runtime
    PUBLIC evmc::evmc
    PUBLIC ethash::keccak
    PUBLIC utils
)
target_include_directories(runtime PUBLIC include)