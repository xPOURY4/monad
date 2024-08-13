function(monad_link_llvm target)
    llvm_config(${target}
       USE_SHARED true)

    target_include_directories(${target}
        SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
endfunction()