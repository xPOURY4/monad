include_directories($ENV{PATH_TKVDB})

# tkvdb
add_library(tkvdb STATIC
    $ENV{PATH_TKVDB}/tkvdb.c
)


add_executable(tkvdb_perf_test
    tkvdb_perf_test.c
)
target_link_libraries(tkvdb_perf_test PUBLIC tkvdb ethash::keccak)