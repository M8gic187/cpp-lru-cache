# cpp-lru-cache

A high-performance, thread-safe LRU (Least Recently Used) cache implemented in modern C++17.

## Features

- **O(1)** `get` and `put` operations via doubly-linked list + hash map
- Thread-safe variant using `std::shared_mutex` for concurrent read access
- Header-only, template-based — works with any hashable key and copyable value type
- Comprehensive unit tests
- Benchmark suite for performance profiling

## Usage

```cpp
#include "lru_cache.hpp"

lru::Cache<int, std::string> cache(3); // capacity = 3

cache.put(1, "one");
cache.put(2, "two");
cache.put(3, "three");

auto val = cache.get(1);   // returns std::optional<std::string>{"one"}
cache.put(4, "four");      // evicts key 2 (least recently used)
```

### Thread-Safe Variant

```cpp
#include "thread_safe_lru_cache.hpp"

lru::ThreadSafeCache<std::string, int> cache(100);
// safe to use from multiple threads
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Performance

Single-threaded, 1M operations on a cache with capacity 10000:

| Operation | Throughput       |
|-----------|-----------------|
| put       | ~120 M ops/sec  |
| get (hit) | ~150 M ops/sec  |
| mixed     | ~100 M ops/sec  |
