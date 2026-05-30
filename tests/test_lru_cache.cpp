#include "lfu_cache.hpp"
#include "lru_cache.hpp"
#include "thread_safe_lfu_cache.hpp"
#include "thread_safe_lru_cache.hpp"
#include "ttl_lru_cache.hpp"
#include "two_queue_cache.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Minimal test harness
static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) void name(); \
    struct Reg_##name { Reg_##name() { run_test(#name, name); } } reg_##name; \
    void name()

void run_test(const char* name, void (*fn)()) {
    ++tests_run;
    try {
        fn();
        ++tests_passed;
        std::cout << "  [PASS] " << name << "\n";
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] " << name << ": " << e.what() << "\n";
    }
}

#define EXPECT(cond) \
    do { if (!(cond)) throw std::runtime_error("Assertion failed: " #cond " at line " + std::to_string(__LINE__)); } while(0)

// ---------------------------------------------------------------------------
// Cache<int, std::string> — basic behaviour
// ---------------------------------------------------------------------------

TEST(cache_basic_put_and_get) {
    lru::Cache<int, std::string> c(3);
    c.put(1, "one");
    c.put(2, "two");
    EXPECT(c.get(1).value() == "one");
    EXPECT(c.get(2).value() == "two");
    EXPECT(!c.get(99).has_value());
}

TEST(cache_evicts_least_recently_used) {
    lru::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2); c.put(3, 3);
    (void)c.get(1); // promote 1 to MRU; order = [1, 3, 2]
    c.put(4, 4); // evicts 2 (LRU)
    EXPECT(!c.get(2).has_value());
    EXPECT(c.get(1).has_value());
    EXPECT(c.get(3).has_value());
    EXPECT(c.get(4).has_value());
}

TEST(cache_update_existing_key) {
    lru::Cache<int, int> c(2);
    c.put(1, 10);
    c.put(1, 20);
    EXPECT(c.get(1).value() == 20);
    EXPECT(c.size() == 1);
}

TEST(cache_update_refreshes_recency) {
    lru::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    c.put(1, 99); // refresh key 1 → it becomes MRU
    c.put(3, 3);  // evicts 2, not 1
    EXPECT(!c.get(2).has_value());
    EXPECT(c.get(1).value() == 99);
}

TEST(cache_capacity_one) {
    lru::Cache<int, int> c(1);
    c.put(1, 1);
    c.put(2, 2);
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).value() == 2);
}

TEST(cache_contains_does_not_change_order) {
    lru::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    (void)c.contains(1);
    c.put(3, 3);
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).has_value());
}

TEST(cache_erase) {
    lru::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    EXPECT(c.erase(1) == true);
    EXPECT(c.erase(1) == false);
    EXPECT(!c.get(1).has_value());
    EXPECT(c.size() == 1);
}

TEST(cache_clear) {
    lru::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    c.clear();
    EXPECT(c.empty());
    EXPECT(c.size() == 0);
    c.put(1, 42);
    EXPECT(c.get(1).value() == 42);
}

TEST(cache_zero_capacity_throws) {
    bool threw = false;
    try { lru::Cache<int, int> c(0); }
    catch (const std::invalid_argument&) { threw = true; }
    EXPECT(threw);
}

TEST(cache_fill_to_capacity) {
    constexpr int N = 100;
    lru::Cache<int, int> c(N);
    for (int i = 0; i < N; ++i) c.put(i, i * 2);
    EXPECT(c.size() == static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) EXPECT(c.get(i).value() == i * 2);
}

TEST(cache_string_keys) {
    lru::Cache<std::string, int> c(3);
    c.put("alpha", 1);
    c.put("beta",  2);
    c.put("gamma", 3);
    c.put("delta", 4); // evicts "alpha" (LRU)
    EXPECT(!c.get("alpha").has_value());
    EXPECT(c.get("beta").value() == 2);
}

// ---------------------------------------------------------------------------
// CacheStats — hit/miss/eviction tracking
// ---------------------------------------------------------------------------

TEST(stats_initial_zero) {
    lru::Cache<int, int> c(4);
    const auto s = c.stats();
    EXPECT(s.hits      == 0);
    EXPECT(s.misses    == 0);
    EXPECT(s.evictions == 0);
    EXPECT(s.puts      == 0);
    EXPECT(s.hit_rate() == 0.0);
}

TEST(stats_tracks_hits_and_misses) {
    lru::Cache<int, int> c(4);
    c.put(1, 10); c.put(2, 20);
    (void)c.get(1);   // hit
    (void)c.get(2);   // hit
    (void)c.get(99);  // miss
    const auto s = c.stats();
    EXPECT(s.hits   == 2);
    EXPECT(s.misses == 1);
}

TEST(stats_hit_rate_calculation) {
    lru::Cache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1); // hit
    (void)c.get(1); // hit
    (void)c.get(2); // miss
    const auto s = c.stats();
    // 2 hits / 3 total ≈ 0.666...
    EXPECT(s.hit_rate() > 0.66 && s.hit_rate() < 0.67);
}

TEST(stats_counts_evictions) {
    lru::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    c.put(3, 3); // evicts 1
    c.put(4, 4); // evicts 2
    EXPECT(c.stats().evictions == 2);
}

TEST(stats_counts_puts) {
    lru::Cache<int, int> c(4);
    c.put(1, 1); c.put(2, 2); c.put(1, 99); // update counts as a put
    EXPECT(c.stats().puts == 3);
}

TEST(stats_reset) {
    lru::Cache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1);
    (void)c.get(99);
    c.reset_stats();
    const auto s = c.stats();
    EXPECT(s.hits == 0 && s.misses == 0 && s.puts == 0 && s.evictions == 0);
}

// ---------------------------------------------------------------------------
// ThreadSafeCache — concurrent correctness
// ---------------------------------------------------------------------------

TEST(thread_safe_concurrent_puts) {
    lru::ThreadSafeCache<int, int> c(1000);
    constexpr int THREADS = 8;
    constexpr int OPS     = 500;

    std::vector<std::thread> workers;
    workers.reserve(THREADS);
    for (int t = 0; t < THREADS; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < OPS; ++i)
                c.put(t * OPS + i, i);
        });
    }
    for (auto& w : workers) w.join();

    EXPECT(c.size() <= 1000);
}

TEST(thread_safe_mixed_reads_writes) {
    lru::ThreadSafeCache<int, int> c(100);
    for (int i = 0; i < 50; ++i) c.put(i, i);

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&]() {
            for (int i = 0; i < 200; ++i) {
                c.put(i % 120, i);
                (void)c.get(i % 50);
                (void)c.contains(i % 100);
            }
        });
    }
    for (auto& w : workers) w.join();
    EXPECT(c.size() <= 100);
}

TEST(thread_safe_stats_accessible) {
    lru::ThreadSafeCache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1);   // hit
    (void)c.get(99);  // miss
    const auto s = c.stats();
    EXPECT(s.hits == 1 && s.misses == 1);
    c.reset_stats();
    EXPECT(c.stats().hits == 0);
}

// ---------------------------------------------------------------------------
// lfu::Cache — basic behaviour
// ---------------------------------------------------------------------------

TEST(lfu_basic_put_and_get) {
    lfu::Cache<int, std::string> c(3);
    c.put(1, "one");
    c.put(2, "two");
    EXPECT(c.get(1).value() == "one");
    EXPECT(c.get(2).value() == "two");
    EXPECT(!c.get(99).has_value());
    EXPECT(c.size() == 2);
}

TEST(lfu_evicts_least_frequently_used) {
    lfu::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2); c.put(3, 3);
    // Access 1 and 2 twice each — 3 stays at frequency 1
    (void)c.get(1); (void)c.get(1);
    (void)c.get(2); (void)c.get(2);
    c.put(4, 4); // evicts 3 (freq=1, LFU)
    EXPECT(!c.get(3).has_value());
    EXPECT(c.get(1).has_value());
    EXPECT(c.get(2).has_value());
    EXPECT(c.get(4).has_value());
}

TEST(lfu_lru_tiebreaking) {
    lfu::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2); c.put(3, 3);
    // All at freq=1; 1 is LRU → should be evicted first
    c.put(4, 4);
    EXPECT(!c.get(1).has_value()); // 1 evicted (oldest at min freq)
    EXPECT(c.get(2).has_value());
    EXPECT(c.get(3).has_value());
    EXPECT(c.get(4).has_value());
}

TEST(lfu_update_increments_frequency) {
    lfu::Cache<int, int> c(2);
    c.put(1, 10); c.put(2, 20);
    (void)c.get(1); // freq(1)=2, freq(2)=1
    c.put(1, 99);   // update: freq(1)=3, value changes
    EXPECT(c.get(1).value() == 99);
    c.put(3, 30); // evicts 2 (freq=1, LFU)
    EXPECT(!c.get(2).has_value());
    EXPECT(c.get(1).has_value());
}

TEST(lfu_zero_capacity_throws) {
    bool threw = false;
    try { lfu::Cache<int, int> c(0); }
    catch (const std::invalid_argument&) { threw = true; }
    EXPECT(threw);
}

TEST(lfu_capacity_one) {
    lfu::Cache<int, int> c(1);
    c.put(1, 1);
    c.put(2, 2); // evicts 1
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).value() == 2);
}

TEST(lfu_erase) {
    lfu::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    EXPECT(c.erase(1) == true);
    EXPECT(c.erase(1) == false);
    EXPECT(!c.get(1).has_value());
    EXPECT(c.size() == 1);
}

TEST(lfu_clear) {
    lfu::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    c.clear();
    EXPECT(c.empty());
    c.put(5, 55);
    EXPECT(c.get(5).value() == 55);
}

TEST(lfu_contains_no_frequency_change) {
    lfu::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    (void)c.contains(1); // should NOT promote 1
    c.put(3, 3);         // 1 is still at freq=1, evict LRU at freq=1 → 1
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).has_value());
}

// ---------------------------------------------------------------------------
// peek() — LRU Cache
// ---------------------------------------------------------------------------

TEST(lru_peek_returns_value_without_updating_recency) {
    lru::Cache<int, int> c(2);
    c.put(1, 10); c.put(2, 20);
    // peek at 1 — if it updated recency, 2 would be evicted instead of 1
    EXPECT(c.peek(1).value() == 10);
    c.put(3, 30); // LRU is still 1 (peek must not promote it)
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).has_value());
    EXPECT(c.get(3).has_value());
}

TEST(lru_peek_returns_nullopt_for_missing_key) {
    lru::Cache<int, int> c(3);
    c.put(1, 1);
    EXPECT(!c.peek(99).has_value());
}

TEST(lru_peek_does_not_affect_stats) {
    lru::Cache<int, int> c(3);
    c.put(1, 1);
    (void)c.peek(1);
    (void)c.peek(99);
    const auto s = c.stats();
    EXPECT(s.hits == 0 && s.misses == 0);
}

// ---------------------------------------------------------------------------
// peek() — LFU Cache
// ---------------------------------------------------------------------------

TEST(lfu_peek_returns_value_without_incrementing_frequency) {
    lfu::Cache<int, int> c(2);
    c.put(1, 10); c.put(2, 20);
    // peek at 1 many times — frequency must stay at 1
    for (int i = 0; i < 5; ++i) EXPECT(c.peek(1).value() == 10);
    // now 2 is accessed via get (freq=2), and 1 is still at freq=1
    (void)c.get(2);
    c.put(3, 30); // evicts 1 (lowest freq=1, peek did not promote it)
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).has_value());
}

TEST(lfu_peek_returns_nullopt_for_missing_key) {
    lfu::Cache<int, int> c(3);
    c.put(1, 1);
    EXPECT(!c.peek(42).has_value());
}

// ---------------------------------------------------------------------------
// lfu::ThreadSafeCache — concurrent correctness
// ---------------------------------------------------------------------------

TEST(thread_safe_lfu_concurrent_puts) {
    lfu::ThreadSafeCache<int, int> c(1000);
    constexpr int THREADS = 8;
    constexpr int OPS     = 200;

    std::vector<std::thread> workers;
    workers.reserve(THREADS);
    for (int t = 0; t < THREADS; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < OPS; ++i)
                c.put(t * OPS + i, i);
        });
    }
    for (auto& w : workers) w.join();
    EXPECT(c.size() <= 1000);
}

TEST(thread_safe_lfu_mixed_reads_writes) {
    lfu::ThreadSafeCache<int, int> c(100);
    for (int i = 0; i < 50; ++i) c.put(i, i);

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&]() {
            for (int i = 0; i < 200; ++i) {
                c.put(i % 120, i);
                (void)c.get(i % 50);
                (void)c.contains(i % 100);
                (void)c.peek(i % 100);
            }
        });
    }
    for (auto& w : workers) w.join();
    EXPECT(c.size() <= 100);
}

TEST(thread_safe_lfu_peek_does_not_require_write_lock) {
    lfu::ThreadSafeCache<int, int> c(50);
    for (int i = 0; i < 50; ++i) c.put(i, i * 2);

    // Concurrent peeks from multiple threads should all succeed
    std::vector<std::thread> readers;
    for (int t = 0; t < 8; ++t) {
        readers.emplace_back([&]() {
            for (int i = 0; i < 50; ++i)
                EXPECT(c.peek(i).value() == i * 2);
        });
    }
    for (auto& r : readers) r.join();
}

// ---------------------------------------------------------------------------
// lru::TtlCache — TTL-based expiry
// ---------------------------------------------------------------------------

TEST(ttl_cache_basic_put_and_get) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, std::string> c(4, ms{500});
    c.put(1, "one");
    c.put(2, "two");
    EXPECT(c.get(1).value() == "one");
    EXPECT(c.get(2).value() == "two");
    EXPECT(!c.get(99).has_value());
    EXPECT(c.size() == 2);
}

TEST(ttl_cache_zero_capacity_throws) {
    using ms = std::chrono::milliseconds;
    bool threw = false;
    try { lru::TtlCache<int, int> c(0, ms{100}); }
    catch (const std::invalid_argument&) { threw = true; }
    EXPECT(threw);
}

TEST(ttl_cache_expired_entry_removed_on_get) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{30});
    c.put(1, 42);
    std::this_thread::sleep_for(ms{50}); // let entry expire
    EXPECT(!c.get(1).has_value());
    EXPECT(c.size() == 0); // lazily removed
}

TEST(ttl_cache_expired_entry_not_visible_via_contains) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{30});
    c.put(1, 42);
    std::this_thread::sleep_for(ms{50});
    EXPECT(!c.contains(1));
}

TEST(ttl_cache_non_expired_entry_survives) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{300});
    c.put(1, 99);
    std::this_thread::sleep_for(ms{20}); // well within TTL
    EXPECT(c.get(1).value() == 99);
}

TEST(ttl_cache_lru_eviction_when_full) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(2, ms{500});
    c.put(1, 1); c.put(2, 2);
    (void)c.get(1); // promote 1 → LRU is now 2
    c.put(3, 3);    // evicts 2
    EXPECT(!c.get(2).has_value());
    EXPECT(c.get(1).has_value());
    EXPECT(c.get(3).has_value());
}

TEST(ttl_cache_custom_ttl_per_entry) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{500});
    c.put(1, 10, ms{30});  // short TTL
    c.put(2, 20, ms{500}); // long TTL
    std::this_thread::sleep_for(ms{60});
    EXPECT(!c.get(1).has_value()); // expired
    EXPECT(c.get(2).value() == 20); // still alive
}

TEST(ttl_cache_purge_expired) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(6, ms{500});
    c.put(1, 1, ms{30});
    c.put(2, 2, ms{30});
    c.put(3, 3, ms{500}); // survives
    std::this_thread::sleep_for(ms{60});
    const std::size_t removed = c.purge_expired();
    EXPECT(removed == 2);
    EXPECT(c.size() == 1);
    EXPECT(c.get(3).has_value());
}

TEST(ttl_cache_update_refreshes_expiry) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{500});
    c.put(1, 10, ms{30}); // will expire soon
    std::this_thread::sleep_for(ms{15});
    c.put(1, 20, ms{500}); // refresh value and TTL
    std::this_thread::sleep_for(ms{30}); // original TTL would have expired here
    EXPECT(c.get(1).value() == 20); // refreshed entry still alive
}

// ---------------------------------------------------------------------------
// get_or_set — lru::Cache
// ---------------------------------------------------------------------------

TEST(lru_get_or_set_hit_returns_cached_value) {
    lru::Cache<int, int> c(4);
    c.put(1, 42);
    int calls = 0;
    auto val = c.get_or_set(1, [&]{ ++calls; return 99; });
    EXPECT(val == 42);
    EXPECT(calls == 0); // factory must NOT be called on a hit
}

TEST(lru_get_or_set_miss_inserts_factory_result) {
    lru::Cache<int, int> c(4);
    int calls = 0;
    auto val = c.get_or_set(7, [&]{ ++calls; return 77; });
    EXPECT(val == 77);
    EXPECT(calls == 1);
    EXPECT(c.get(7).value() == 77); // value was cached
}

TEST(lru_get_or_set_tracks_stats) {
    lru::Cache<int, int> c(4);
    c.put(1, 10);
    c.get_or_set(1, []{ return 0; }); // hit
    c.get_or_set(2, []{ return 20; }); // miss + put
    const auto s = c.stats();
    EXPECT(s.hits   == 1);
    EXPECT(s.misses == 1);
    EXPECT(s.puts   == 2); // initial put + get_or_set miss
}

TEST(lru_get_or_set_evicts_lru_when_full) {
    lru::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    // key 1 is LRU; inserting 3 via get_or_set should evict 1
    c.get_or_set(3, []{ return 3; });
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).has_value());
    EXPECT(c.get(3).value() == 3);
}

// ---------------------------------------------------------------------------
// get_or_set — lfu::Cache
// ---------------------------------------------------------------------------

TEST(lfu_get_or_set_hit_promotes_frequency) {
    lfu::Cache<int, int> c(3);
    c.put(1, 10); c.put(2, 20); c.put(3, 30);
    // promote key 1 twice via get_or_set
    c.get_or_set(1, []{ return 0; });
    c.get_or_set(1, []{ return 0; });
    // 1 now has freq=3, 2 and 3 have freq=1
    c.put(4, 40); // evicts LFU (2 or 3, not 1)
    EXPECT(c.get(1).has_value());
}

TEST(lfu_get_or_set_miss_caches_result) {
    lfu::Cache<int, int> c(3);
    int calls = 0;
    auto v = c.get_or_set(5, [&]{ ++calls; return 55; });
    EXPECT(v == 55);
    EXPECT(calls == 1);
    EXPECT(c.get(5).value() == 55);
}

// ---------------------------------------------------------------------------
// resize — lru::Cache
// ---------------------------------------------------------------------------

TEST(lru_resize_grow_accepts_more_entries) {
    lru::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    c.resize(4);
    EXPECT(c.capacity() == 4);
    c.put(3, 3); c.put(4, 4);
    EXPECT(c.size() == 4);
    EXPECT(c.get(1).has_value());
}

TEST(lru_resize_shrink_evicts_lru_entries) {
    lru::Cache<int, int> c(4);
    c.put(1, 1); c.put(2, 2); c.put(3, 3); c.put(4, 4);
    // Access 3 and 4 to make 1 and 2 the LRU entries
    (void)c.get(3); (void)c.get(4);
    c.resize(2); // evicts 1 and 2 (LRU)
    EXPECT(c.size() == 2);
    EXPECT(c.capacity() == 2);
    EXPECT(!c.get(1).has_value());
    EXPECT(!c.get(2).has_value());
    EXPECT(c.get(3).has_value());
    EXPECT(c.get(4).has_value());
}

TEST(lru_resize_shrink_updates_eviction_stats) {
    lru::Cache<int, int> c(4);
    c.put(1, 1); c.put(2, 2); c.put(3, 3);
    c.reset_stats();
    c.resize(1); // must evict 2 entries
    EXPECT(c.stats().evictions == 2);
}

TEST(lru_resize_to_zero_throws) {
    lru::Cache<int, int> c(4);
    bool threw = false;
    try { c.resize(0); } catch (const std::invalid_argument&) { threw = true; }
    EXPECT(threw);
}

TEST(lru_resize_same_capacity_is_noop) {
    lru::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    c.resize(3);
    EXPECT(c.size() == 2);
    EXPECT(c.capacity() == 3);
}

// ---------------------------------------------------------------------------
// TtlCache — statistics and peek()
// ---------------------------------------------------------------------------

TEST(ttl_cache_stats_initial_zero) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{500});
    const auto s = c.stats();
    EXPECT(s.hits == 0 && s.misses == 0 && s.puts == 0);
    EXPECT(s.evictions == 0 && s.expirations == 0);
    EXPECT(s.hit_rate() == 0.0);
}

TEST(ttl_cache_stats_tracks_hits_and_misses) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{500});
    c.put(1, 10);
    (void)c.get(1);  // hit
    (void)c.get(99); // miss (key absent)
    const auto s = c.stats();
    EXPECT(s.hits   == 1);
    EXPECT(s.misses == 1);
    EXPECT(s.puts   == 1);
}

TEST(ttl_cache_stats_counts_expirations_on_get) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{30});
    c.put(1, 42);
    std::this_thread::sleep_for(ms{60});
    (void)c.get(1); // expired → miss + expiration
    const auto s = c.stats();
    EXPECT(s.misses      == 1);
    EXPECT(s.expirations == 1);
    EXPECT(s.hits        == 0);
}

TEST(ttl_cache_stats_counts_expirations_via_purge) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(6, ms{500});
    c.put(1, 1, ms{20});
    c.put(2, 2, ms{20});
    c.put(3, 3, ms{500});
    c.reset_stats();
    std::this_thread::sleep_for(ms{50});
    c.purge_expired();
    EXPECT(c.stats().expirations == 2);
}

TEST(ttl_cache_stats_counts_lru_evictions) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(2, ms{500});
    c.put(1, 1); c.put(2, 2);
    c.reset_stats();
    c.put(3, 3); // evicts LRU entry (key 1)
    EXPECT(c.stats().evictions   == 1);
    EXPECT(c.stats().expirations == 0);
}

TEST(ttl_cache_stats_reset) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{500});
    c.put(1, 1);
    (void)c.get(1);
    (void)c.get(99);
    c.reset_stats();
    const auto s = c.stats();
    EXPECT(s.hits == 0 && s.misses == 0 && s.puts == 0 && s.expirations == 0);
}

TEST(ttl_cache_peek_returns_value_without_side_effects) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(2, ms{500});
    c.put(1, 10); c.put(2, 20);
    // peek at 1; if it updated recency, 2 would be evicted instead
    EXPECT(c.peek(1).value() == 10);
    c.put(3, 30); // LRU is still 1 → evicted
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).has_value());
}

TEST(ttl_cache_peek_returns_nullopt_for_expired_entry) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{30});
    c.put(1, 99);
    std::this_thread::sleep_for(ms{60});
    EXPECT(!c.peek(1).has_value()); // expired
    EXPECT(c.size() == 1);         // peek must NOT remove the entry
}

TEST(ttl_cache_peek_does_not_affect_stats) {
    using ms = std::chrono::milliseconds;
    lru::TtlCache<int, int> c(4, ms{500});
    c.put(1, 1);
    c.reset_stats();
    (void)c.peek(1);
    (void)c.peek(99);
    const auto s = c.stats();
    EXPECT(s.hits == 0 && s.misses == 0);
}

// ---------------------------------------------------------------------------
// lfu::Cache — CacheStats tracking
// ---------------------------------------------------------------------------

TEST(lfu_stats_initial_zero) {
    lfu::Cache<int, int> c(4);
    const auto s = c.stats();
    EXPECT(s.hits      == 0);
    EXPECT(s.misses    == 0);
    EXPECT(s.evictions == 0);
    EXPECT(s.puts      == 0);
    EXPECT(s.hit_rate() == 0.0);
}

TEST(lfu_stats_tracks_hits_and_misses) {
    lfu::Cache<int, int> c(4);
    c.put(1, 10); c.put(2, 20);
    (void)c.get(1);   // hit
    (void)c.get(2);   // hit
    (void)c.get(99);  // miss
    const auto s = c.stats();
    EXPECT(s.hits   == 2);
    EXPECT(s.misses == 1);
}

TEST(lfu_stats_hit_rate_calculation) {
    lfu::Cache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1); // hit
    (void)c.get(1); // hit
    (void)c.get(2); // miss
    const auto s = c.stats();
    EXPECT(s.hit_rate() > 0.66 && s.hit_rate() < 0.67);
}

TEST(lfu_stats_counts_evictions) {
    lfu::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    c.put(3, 3); // evicts LFU (key 1, freq=1)
    c.put(4, 4); // evicts LFU (key 2 or 3, freq=1)
    EXPECT(c.stats().evictions == 2);
}

TEST(lfu_stats_counts_puts) {
    lfu::Cache<int, int> c(4);
    c.put(1, 1); c.put(2, 2); c.put(1, 99); // update also counts as a put
    EXPECT(c.stats().puts == 3);
}

TEST(lfu_stats_reset) {
    lfu::Cache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1);
    (void)c.get(99);
    c.reset_stats();
    const auto s = c.stats();
    EXPECT(s.hits == 0 && s.misses == 0 && s.puts == 0 && s.evictions == 0);
}

TEST(lfu_stats_get_or_set_tracking) {
    lfu::Cache<int, int> c(4);
    c.put(1, 10);
    c.get_or_set(1, []{ return 0; }); // hit
    c.get_or_set(2, []{ return 20; }); // miss + put
    const auto s = c.stats();
    EXPECT(s.hits   == 1);
    EXPECT(s.misses == 1);
    EXPECT(s.puts   == 2); // initial put + get_or_set miss
}

// ---------------------------------------------------------------------------
// lfu::Cache — resize()
// ---------------------------------------------------------------------------

TEST(lfu_resize_grow_accepts_more_entries) {
    lfu::Cache<int, int> c(2);
    c.put(1, 1); c.put(2, 2);
    c.resize(4);
    EXPECT(c.capacity() == 4);
    c.put(3, 3); c.put(4, 4);
    EXPECT(c.size() == 4);
    EXPECT(c.get(1).has_value());
}

TEST(lfu_resize_shrink_evicts_lfu_entries) {
    lfu::Cache<int, int> c(4);
    c.put(1, 1); c.put(2, 2); c.put(3, 3); c.put(4, 4);
    // Access 3 and 4 to give them higher frequency
    (void)c.get(3); (void)c.get(3);
    (void)c.get(4); (void)c.get(4);
    c.resize(2); // evicts LFU entries (1 and 2 with freq=1)
    EXPECT(c.size() == 2);
    EXPECT(c.capacity() == 2);
    EXPECT(!c.get(1).has_value());
    EXPECT(!c.get(2).has_value());
    EXPECT(c.get(3).has_value());
    EXPECT(c.get(4).has_value());
}

TEST(lfu_resize_shrink_updates_eviction_stats) {
    lfu::Cache<int, int> c(4);
    c.put(1, 1); c.put(2, 2); c.put(3, 3);
    c.reset_stats();
    c.resize(1); // must evict 2 entries
    EXPECT(c.stats().evictions == 2);
}

TEST(lfu_resize_to_zero_throws) {
    lfu::Cache<int, int> c(4);
    bool threw = false;
    try { c.resize(0); } catch (const std::invalid_argument&) { threw = true; }
    EXPECT(threw);
}

TEST(lfu_resize_same_capacity_is_noop) {
    lfu::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    c.resize(3);
    EXPECT(c.size() == 2);
    EXPECT(c.capacity() == 3);
}

// ---------------------------------------------------------------------------
// TwoQueueCache<K,V> — basic behaviour
// ---------------------------------------------------------------------------

TEST(twoq_basic_put_and_get) {
    // capacity=8 → a1in_capacity=2, am_capacity=6; both keys fit in A1in
    lru::TwoQueueCache<int, std::string> c(8);
    c.put(1, "one");
    c.put(2, "two");
    EXPECT(c.get(1).value() == "one");
    EXPECT(c.get(2).value() == "two");
    EXPECT(!c.get(99).has_value());
}

TEST(twoq_capacity_zero_throws) {
    try {
        lru::TwoQueueCache<int, int> c(0);
        EXPECT(false); // should not reach here
    } catch (const std::invalid_argument&) {}
}

TEST(twoq_size_and_empty) {
    lru::TwoQueueCache<int, int> c(4);
    EXPECT(c.empty());
    EXPECT(c.size() == 0);
    c.put(1, 1);
    EXPECT(!c.empty());
    EXPECT(c.size() == 1);
}

TEST(twoq_contains) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 1);
    EXPECT(c.contains(1));
    EXPECT(!c.contains(2));
}

TEST(twoq_peek_does_not_promote) {
    lru::TwoQueueCache<int, int> c(8); // a1in_capacity=2, both keys fit
    c.put(1, 1); c.put(2, 2);
    EXPECT(c.peek(1).value() == 1);
    EXPECT(c.peek(99) == std::nullopt);
}

TEST(twoq_update_existing_in_a1in) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 10);
    c.put(1, 20); // update within A1in
    EXPECT(c.get(1).value() == 20);
    EXPECT(c.size() == 1);
}

TEST(twoq_update_existing_in_am) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 10);
    // Force promotion to Am: evict from A1in so key appears in A1out,
    // then re-insert to trigger promotion.
    // With capacity=4, a1in_capacity=1, a1out=3:
    lru::TwoQueueCache<int, int> c2(4, 0.25);
    c2.put(1, 100);          // A1in: [1]
    c2.put(2, 200);          // A1in full → 1 demoted to A1out; A1in: [2]
    c2.put(1, 999);          // 1 is in A1out → promote to Am with new value
    EXPECT(c2.get(1).value() == 999);
    c2.put(1, 777);          // now in Am → update in Am
    EXPECT(c2.get(1).value() == 777);
}

// ---------------------------------------------------------------------------
// TwoQueueCache — eviction and promotion
// ---------------------------------------------------------------------------

TEST(twoq_a1in_eviction_to_a1out) {
    // capacity=4, a1in=1, am=3 → a1in full after first insert
    lru::TwoQueueCache<int, int> c(4, 0.25);
    c.put(1, 1); // A1in: [1]
    c.put(2, 2); // A1in: [2], 1 demoted to A1out (value dropped)
    EXPECT(c.get(2).has_value());
    // 1 is in A1out (ghost only) — get() should miss
    EXPECT(!c.get(1).has_value());
    EXPECT(c.a1out_size() == 1);
}

TEST(twoq_promotion_from_a1out_to_am) {
    lru::TwoQueueCache<int, int> c(4, 0.25);
    c.put(1, 1);  // A1in: [1]
    c.put(2, 2);  // 1 demoted to A1out; A1in: [2]
    c.put(1, 11); // 1 in A1out → promote to Am with value 11
    EXPECT(c.get(1).value() == 11); // now in Am
    EXPECT(c.am_size() == 1);
}

TEST(twoq_am_eviction_when_full) {
    // capacity=4, a1in=1, am=3
    lru::TwoQueueCache<int, int> c(4, 0.25);
    // Fill A1in and flush each key to A1out, then promote to Am to fill it.
    // Keys: 10,20,30 → each put twice ends up in Am
    // Step 1: push 10 through A1in → A1out
    c.put(10, 10); // A1in:[10]
    c.put(99, 0);  // A1in:[99], 10→A1out
    c.put(10, 10); // 10 in A1out → Am:[10]
    // Step 2: push 20
    c.put(98, 0);  // A1in:[98], 99→A1out; Am:[10]
    c.put(20, 20); // A1in:[20], 98→A1out; Am:[10]
    c.put(20, 20); // 20 in A1out? 20 is still in A1in actually. Let me reconsider.
    // Am currently has [10]; A1in has [20]; size=2
    c.put(97, 0);  // A1in:[97], 20→A1out; size=2 (Am:[10], A1in:[97])
    c.put(20, 20); // 20 in A1out → Am:[20,10]; size=3
    c.put(96, 0);  // A1in:[96], 97→A1out; size=3
    c.put(30, 30); // A1in:[30], 96→A1out; am_size=2, a1in_size=1 → size=3
    // Force 30 to Am: another key into a1in
    c.put(95, 0);  // 30→A1out; A1in:[95]
    c.put(30, 30); // 30 in A1out → Am:[30,20,10]; am_size=3 (full!), a1in:[95]
    EXPECT(c.am_size() == 3);
    // Now push another key through to Am → should evict LRU of Am (10)
    c.put(94, 0); // 95→A1out; A1in:[94]
    c.put(95, 0); // 95 in A1out → Am:[95,30,20]; 10 evicted; a1in:[94]
    EXPECT(!c.get(10).has_value()); // 10 was LRU of Am, evicted
    EXPECT(c.get(30).has_value());
    EXPECT(c.get(20).has_value());
}

TEST(twoq_capacity_one) {
    lru::TwoQueueCache<int, int> c(1);
    c.put(1, 1);
    c.put(2, 2); // evicts 1
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).value() == 2);
}

// ---------------------------------------------------------------------------
// TwoQueueCache — statistics
// ---------------------------------------------------------------------------

TEST(twoq_stats_hits_misses) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1);  // hit
    (void)c.get(99); // miss
    EXPECT(c.stats().hits   == 1);
    EXPECT(c.stats().misses == 1);
    EXPECT(c.stats().puts   == 1);
}

TEST(twoq_stats_evictions) {
    lru::TwoQueueCache<int, int> c(2, 0.5); // a1in=1, am=1
    c.put(1, 1); // A1in:[1]
    c.put(2, 2); // A1in:[2], 1→A1out (eviction +1)
    c.put(3, 3); // A1in:[3], 2→A1out (eviction +1)
    EXPECT(c.stats().evictions == 2);
}

TEST(twoq_stats_hit_rate) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1);  // hit
    (void)c.get(1);  // hit
    (void)c.get(99); // miss
    // hit_rate = 2/3
    EXPECT(c.stats().hit_rate() > 0.66 && c.stats().hit_rate() < 0.68);
}

TEST(twoq_reset_stats) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 1);
    (void)c.get(1);
    (void)c.get(99);
    c.reset_stats();
    auto s = c.stats();
    EXPECT(s.hits == 0 && s.misses == 0 && s.puts == 0 && s.evictions == 0);
}

TEST(twoq_stats_empty_hit_rate_is_zero) {
    lru::TwoQueueCache<int, int> c(4);
    EXPECT(c.stats().hit_rate() == 0.0);
}

// ---------------------------------------------------------------------------
// TwoQueueCache — erase and clear
// ---------------------------------------------------------------------------

TEST(twoq_erase_from_a1in) {
    lru::TwoQueueCache<int, int> c(8); // a1in_capacity=2, both keys fit in A1in
    c.put(1, 1); c.put(2, 2);
    EXPECT(c.erase(1) == true);
    EXPECT(c.erase(1) == false);
    EXPECT(!c.get(1).has_value());
    EXPECT(c.size() == 1);
}

TEST(twoq_erase_from_am) {
    lru::TwoQueueCache<int, int> c(4, 0.25);
    c.put(1, 1); // A1in:[1]
    c.put(9, 0); // A1in:[9], 1→A1out
    c.put(1, 1); // 1 in A1out → Am:[1]
    EXPECT(c.erase(1) == true);
    EXPECT(!c.get(1).has_value());
    EXPECT(c.am_size() == 0);
}

TEST(twoq_erase_nonexistent_returns_false) {
    lru::TwoQueueCache<int, int> c(4);
    EXPECT(c.erase(42) == false);
}

TEST(twoq_clear) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 1); c.put(2, 2); c.put(3, 3);
    c.clear();
    EXPECT(c.empty());
    EXPECT(c.size() == 0);
    EXPECT(c.am_size() == 0);
    EXPECT(c.a1in_size() == 0);
    EXPECT(c.a1out_size() == 0);
}

// ---------------------------------------------------------------------------
// TwoQueueCache — get_or_set
// ---------------------------------------------------------------------------

TEST(twoq_get_or_set_miss_inserts) {
    lru::TwoQueueCache<int, int> c(4);
    int calls = 0;
    int v = c.get_or_set(1, [&]{ ++calls; return 42; });
    EXPECT(v == 42);
    EXPECT(calls == 1);
    EXPECT(c.contains(1));
}

TEST(twoq_get_or_set_hit_skips_factory) {
    lru::TwoQueueCache<int, int> c(4);
    c.put(1, 99);
    int calls = 0;
    int v = c.get_or_set(1, [&]{ ++calls; return 0; });
    EXPECT(v == 99);
    EXPECT(calls == 0);
}

TEST(twoq_get_or_set_miss_via_a1out_promotes_to_am) {
    lru::TwoQueueCache<int, int> c(4, 0.25);
    c.put(1, 1);  // A1in:[1]
    c.put(9, 0);  // 1→A1out
    int v = c.get_or_set(1, []{ return 55; }); // 1 in A1out → Am
    EXPECT(v == 55);
    EXPECT(c.am_size() == 1);
}

// ---------------------------------------------------------------------------
// TwoQueueCache — sub-queue capacity accessors
// ---------------------------------------------------------------------------

TEST(twoq_capacity_accessors) {
    lru::TwoQueueCache<int, int> c(8, 0.25); // a1in=2, am=6
    EXPECT(c.capacity() == 8);
    EXPECT(c.a1in_capacity() == 2);
    EXPECT(c.am_capacity() == 6);
}

TEST(twoq_default_ratio_splits_correctly) {
    // capacity=4, default ratio 0.25 → a1in=1, am=3
    lru::TwoQueueCache<int, int> c(4);
    EXPECT(c.a1in_capacity() == 1);
    EXPECT(c.am_capacity() == 3);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "Running LRU/LFU Cache tests...\n";
    std::cout << "\n--- Cache<K,V> ---\n";

    std::cout << "\nResults: " << tests_passed << "/" << tests_run << " passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
