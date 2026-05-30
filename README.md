# cpp-lru-cache

A high-performance, header-only cache library in modern C++17 offering multiple
eviction policies — **LRU**, **LFU**, **2Q (Two-Queue)**, and **TTL** — with
built-in access statistics and optional thread-safe wrappers.

## Features

- **O(1)** `get` and `put` for LRU, LFU, and 2Q via doubly-linked lists + hash maps
- **LFU** uses per-frequency buckets with LRU tie-breaking (no extra cost)
- **TwoQueueCache** — pollution-resistant 2Q algorithm: separates "recent" from
  "frequent" items using three sub-queues (A1in FIFO, A1out ghost, Am LRU);
  prevents sequential-scan pollution of the hot working set
- **Thread-safe** wrappers for LRU and LFU using `std::shared_mutex`
- **`get_or_set(key, factory)`** — atomically returns a cached value or calls a factory to insert it; factory invoked at most once, even under concurrency
- **`resize(n)`** — dynamically adjusts LRU cache capacity; LRU entries are evicted when shrinking
- **`peek(key)`** — non-mutating lookup that does not update recency or statistics
- **Built-in access statistics** — hits, misses, evictions, TTL expirations, put count, hit rate; uniform `CacheStats` API across all variants
- **`TtlCache`** — LRU cache with per-entry TTL; lazy expiry on access, eager `purge_expired()`, full stats API
- Header-only, template-based — works with any hashable key and move-constructible value
- Comprehensive unit tests (103 cases) covering edge cases and concurrent correctness
- Benchmark suite comparing LRU vs LFU throughput

### Algorithm comparison

| Variant             | Header                       | Eviction policy         | Scan-resistant | Thread-safe |
|---------------------|------------------------------|-------------------------|:--------------:|:-----------:|
| `lru::Cache`        | `lru_cache.hpp`              | Least Recently Used     | no             | no          |
| `lfu::Cache`        | `lfu_cache.hpp`              | Least Frequently Used   | partial        | no          |
| `lru::TwoQueueCache`| `two_queue_cache.hpp`        | 2Q (LRU + FIFO + ghost) | **yes**        | no          |
| `lru::TtlCache`     | `ttl_lru_cache.hpp`          | LRU + per-entry TTL     | no             | no          |
| `lru::ThreadSafeCache` | `thread_safe_lru_cache.hpp` | LRU (shared_mutex)     | no             | **yes**     |
| `lfu::ThreadSafeCache` | `thread_safe_lfu_cache.hpp` | LFU (shared_mutex)     | partial        | **yes**     |

## Usage

### LRU Cache

```cpp
#include "lru_cache.hpp"

lru::Cache<int, std::string> cache(3); // capacity = 3

cache.put(1, "one");
cache.put(2, "two");
cache.put(3, "three");

auto val = cache.get(1);   // std::optional<std::string>{"one"}
cache.put(4, "four");      // evicts key 2 (least recently used)
```

### LFU Cache

```cpp
#include "lfu_cache.hpp"

lfu::Cache<int, std::string> cache(3);

cache.put(1, "one");
cache.put(2, "two");
cache.put(3, "three");

cache.get(1); cache.get(1); // freq(1) = 3
cache.get(2);               // freq(2) = 2
                            // freq(3) = 1  ← evicted next
cache.put(4, "four");       // evicts key 3 (least frequently used)
```

### get_or_set — atomic read-or-insert

```cpp
lru::Cache<std::string, Config> cache(256);

// Returns cached config if present; loads from disk and caches it if not.
// The loader lambda is called at most once per missing key.
Config cfg = cache.get_or_set("service.json", []() {
    return load_config_from_disk("service.json");
});
```

Works on `lru::Cache`, `lfu::Cache`, and both thread-safe wrappers. The
thread-safe variants hold the exclusive lock across the entire lookup+insert
so the factory is never called twice for the same key under concurrency.

### resize — dynamic capacity adjustment

```cpp
lru::Cache<int, int> cache(100);
// ... workload changes ...
cache.resize(50);  // evicts 50 LRU entries, updates capacity
cache.resize(200); // just raises the ceiling, no evictions
```

### Access Statistics

```cpp
lru::Cache<int, int> cache(100);
// ... use the cache ...

lru::CacheStats s = cache.stats();
std::cout << "hits="        << s.hits
          << " misses="     << s.misses
          << " evictions="  << s.evictions   // LRU capacity evictions
          << " expirations="<< s.expirations // TTL expirations (TtlCache)
          << " hit_rate="   << s.hit_rate() << "\n";

cache.reset_stats(); // zero all counters
```

Statistics are also available on `ThreadSafeCache` and `TtlCache` via the
same `stats()` / `reset_stats()` API.

### Two-Queue (2Q) Cache

```cpp
#include "two_queue_cache.hpp"

// capacity=1000; A1in gets 25% (250 slots), Am gets 75% (750 slots)
lru::TwoQueueCache<std::string, Data> cache(1000);

// First access → goes to A1in (FIFO newcomer queue)
cache.put("key1", data1);

// A key that was in A1in and later evicted lands in the A1out ghost queue.
// Re-inserting it bypasses A1in and goes directly into Am (frequent set).
cache.put("key1", updated_data); // promoted to Am if key1 was in A1out

// Custom split: 40% A1in, 60% Am
lru::TwoQueueCache<int, int> cache2(500, 0.40);
```

**When to prefer 2Q over plain LRU:**
- Workloads that mix a hot working set with occasional large sequential scans
  (e.g. database full-table scans, batch ETL, log processing)
- Any scenario where one-time accesses should not displace frequently used entries

### Thread-Safe LRU

```cpp
#include "thread_safe_lru_cache.hpp"

lru::ThreadSafeCache<std::string, int> cache(100);
// safe to use from multiple threads concurrently
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/benchmarks/bench_lru_cache
```

## Performance

Single-threaded, 1 M operations, capacity = 10 000:

| Operation                       | LRU throughput   |
|---------------------------------|-----------------|
| put (fill, no eviction)         | ~120 M ops/sec  |
| put (with eviction)             | ~110 M ops/sec  |
| get (100 % hit rate)            | ~150 M ops/sec  |
| get (~50 % hit rate, random)    | ~130 M ops/sec  |
| mixed put/get (50/50)           | ~100 M ops/sec  |
| ThreadSafeCache single-threaded | ~70 M ops/sec   |
| ThreadSafeCache 4 threads total | ~120 M ops/sec  |

LFU throughput is comparable to LRU for workloads with stable frequency
distributions; it trades a slightly larger constant factor (two hash map
lookups per promotion) for more accurate eviction under skewed access patterns.
