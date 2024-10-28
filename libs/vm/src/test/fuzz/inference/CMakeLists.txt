add_executable(fuzz-inference main.cpp)

monad_compile_options(fuzz-inference)
monad_link_llvm(fuzz-inference)

target_link_libraries(fuzz-inference
    PRIVATE
        compiler
        utils
        CLI11::CLI11
)


add_executable(fuzz-inference-argparse main.cpp)

monad_compile_options(fuzz-inference-argparse)
monad_link_llvm(fuzz-inference-argparse)

target_link_libraries(fuzz-inference-argparse
    PRIVATE
        compiler
        utils
        CLI11::CLI11
)

target_compile_options(fuzz-inference-argparse PRIVATE -DAFL_PERSISTENT_REPLAY_ARGPARSE)