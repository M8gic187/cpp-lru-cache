add_executable(bench_lru_cache bench_lru_cache.cpp)
target_include_directories(bench_lru_cache PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(bench_lru_cache PRIVATE -O3 -DNDEBUG)
