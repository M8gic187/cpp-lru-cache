#include "lru_cache.hpp"
#include "thread_safe_lru_cache.hpp"

#include <cassert>
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

// Helper: assert with message
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
    // peek at 1 without promoting; next put should evict 1
    (void)c.contains(1);
    c.put(3, 3);
    EXPECT(!c.get(1).has_value());
    EXPECT(c.get(2).has_value());
}

TEST(cache_erase) {
    lru::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    EXPECT(c.erase(1) == true);
    EXPECT(c.erase(1) == false); // already gone
    EXPECT(!c.get(1).has_value());
    EXPECT(c.size() == 1);
}

TEST(cache_clear) {
    lru::Cache<int, int> c(3);
    c.put(1, 1); c.put(2, 2);
    c.clear();
    EXPECT(c.empty());
    EXPECT(c.size() == 0);
    // Cache still usable after clear
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

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "Running LRU Cache tests...\n";
    std::cout << "\n--- Cache<K,V> ---\n";

    // Tests are self-registering via static initializers
    // (already run by the time main() starts)

    std::cout << "\nResults: " << tests_passed << "/" << tests_run << " passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
