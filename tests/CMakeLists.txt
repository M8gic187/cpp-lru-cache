add_executable(test_lru_cache test_lru_cache.cpp)
target_include_directories(test_lru_cache PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME lru_cache_tests COMMAND test_lru_cache)
