#include "lru_cache.hpp"
#include "thread_safe_lru_cache.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::high_resolution_clock;

struct Result {
    std::string label;
    double      ops_per_sec;
};

void print_result(const Result& r) {
    std::cout << std::left  << std::setw(40) << r.label
              << std::right << std::setw(12) << std::fixed << std::setprecision(2)
              << (r.ops_per_sec / 1e6) << " M ops/sec\n";
}

template <typename Fn>
double measure(Fn&& fn, std::size_t ops) {
    auto t0 = Clock::now();
    fn();
    auto t1 = Clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return static_cast<double>(ops) / secs;
}

// xorshift64 for fast pseudo-random numbers without stdlib overhead
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed = 42) : state(seed) {}
    uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
};

} // namespace

int main() {
    constexpr std::size_t CAPACITY = 10'000;
    constexpr std::size_t N        = 1'000'000;

    std::cout << "LRU Cache Benchmark (capacity=" << CAPACITY
              << ", N=" << N << ")\n";
    std::cout << std::string(55, '-') << "\n";

    // --- sequential put (no eviction) ---
    {
        lru::Cache<int, int> c(CAPACITY);
        auto ops = measure([&] {
            for (std::size_t i = 0; i < CAPACITY; ++i)
                c.put(static_cast<int>(i), static_cast<int>(i));
        }, CAPACITY);
        print_result({"put (fill, no eviction)", ops});
    }

    // --- put with eviction ---
    {
        lru::Cache<int, int> c(CAPACITY);
        // Pre-fill
        for (std::size_t i = 0; i < CAPACITY; ++i)
            c.put(static_cast<int>(i), static_cast<int>(i));

        auto ops = measure([&] {
            for (std::size_t i = CAPACITY; i < CAPACITY + N; ++i)
                c.put(static_cast<int>(i), static_cast<int>(i));
        }, N);
        print_result({"put (with eviction)", ops});
    }

    // --- get 100% hit ---
    {
        lru::Cache<int, int> c(CAPACITY);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            c.put(static_cast<int>(i), static_cast<int>(i));

        volatile int sink = 0;
        auto ops = measure([&] {
            for (std::size_t i = 0; i < N; ++i) {
                auto v = c.get(static_cast<int>(i % CAPACITY));
                if (v) sink += *v;
            }
        }, N);
        (void)sink;
        print_result({"get (100% hit rate)", ops});
    }

    // --- get 50% hit (random access over 2x capacity) ---
    {
        lru::Cache<int, int> c(CAPACITY);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            c.put(static_cast<int>(i), static_cast<int>(i));

        Rng rng;
        volatile int sink = 0;
        auto ops = measure([&] {
            for (std::size_t i = 0; i < N; ++i) {
                auto v = c.get(static_cast<int>(rng.next() % (CAPACITY * 2)));
                if (v) sink += *v;
            }
        }, N);
        (void)sink;
        print_result({"get (~50% hit rate, random)", ops});
    }

    // --- mixed put/get ---
    {
        lru::Cache<int, int> c(CAPACITY);
        Rng rng;
        auto ops = measure([&] {
            for (std::size_t i = 0; i < N; ++i) {
                int k = static_cast<int>(rng.next() % (CAPACITY * 2));
                if (i % 2 == 0) c.put(k, k);
                else             (void)c.get(k);
            }
        }, N);
        print_result({"mixed put/get (50/50)", ops});
    }

    // --- thread-safe: single-threaded overhead ---
    {
        lru::ThreadSafeCache<int, int> c(CAPACITY);
        Rng rng;
        auto ops = measure([&] {
            for (std::size_t i = 0; i < N; ++i) {
                int k = static_cast<int>(rng.next() % (CAPACITY * 2));
                if (i % 2 == 0) c.put(k, k);
                else             (void)c.get(k);
            }
        }, N);
        print_result({"ThreadSafeCache single-threaded", ops});
    }

    // --- thread-safe: 4-thread concurrent ---
    {
        constexpr int THREADS = 4;
        lru::ThreadSafeCache<int, int> c(CAPACITY);

        auto t0 = Clock::now();
        std::vector<std::thread> workers;
        workers.reserve(THREADS);
        for (int t = 0; t < THREADS; ++t) {
            workers.emplace_back([&] {
                Rng rng;
                for (std::size_t i = 0; i < N / THREADS; ++i) {
                    int k = static_cast<int>(rng.next() % (CAPACITY * 2));
                    if (i % 2 == 0) c.put(k, k);
                    else             (void)c.get(k);
                }
            });
        }
        for (auto& w : workers) w.join();
        auto t1  = Clock::now();
        double s = std::chrono::duration<double>(t1 - t0).count();
        double ops = static_cast<double>(N) / s;
        print_result({"ThreadSafeCache 4 threads (total)", ops});
    }

    std::cout << std::string(55, '-') << "\n";
    return 0;
}
