add_library(runtime runtime.c)

monad_compile_options(runtime)

target_link_libraries(runtime PUBLIC evmc::evmc)
target_include_directories(runtime PUBLIC include)