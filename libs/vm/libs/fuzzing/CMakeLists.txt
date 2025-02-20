add_library(monad-fuzzing STATIC)

target_sources(monad-fuzzing PRIVATE
  "src/monad/fuzzing/generator/choice.hpp"
  "src/monad/fuzzing/generator/generator.hpp"
  "src/monad/fuzzing/generator/instruction_data.cpp"
  "src/monad/fuzzing/generator/instruction_data.hpp"
)

monad_compile_options(monad-fuzzing)

target_include_directories(monad-fuzzing
  PUBLIC src/
)

target_link_libraries(monad-fuzzing
  PUBLIC monad-compiler
  PUBLIC utils
)