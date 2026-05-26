#pragma once

#include "lfu_cache.hpp"

#include <mutex>
#include <optional>
#include <shared_mutex>

namespace lfu {

// Thread-safe wrapper around lfu::Cache<K,V>.
//
// Concurrency model mirrors lru::ThreadSafeCache:
//   - contains() and peek() share a read lock (do not mutate internal state).
//   - get() and put() promote/evict entries and therefore require an exclusive lock.
//   - erase() and clear() acquire an exclusive lock.
//
// All operations are safe to call concurrently from multiple threads.
template <typename Key, typename Value>
class ThreadSafeCache {
public:
    using size_type = typename Cache<Key, Value>::size_type;
    using freq_type = typename Cache<Key, Value>::freq_type;

    explicit ThreadSafeCache(size_type capacity) : cache_(capacity) {}

    ThreadSafeCache(const ThreadSafeCache&)            = delete;
    ThreadSafeCache& operator=(const ThreadSafeCache&) = delete;

    // get() increments frequency — needs exclusive lock.
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        std::unique_lock lock(mutex_);
        return cache_.get(key);
    }

    void put(const Key& key, Value value) {
        std::unique_lock lock(mutex_);
        cache_.put(key, std::move(value));
    }

    // Thread-safe get_or_set: holds the exclusive lock for the full lookup+insert.
    template <typename Factory>
    Value get_or_set(const Key& key, Factory&& factory) {
        std::unique_lock lock(mutex_);
        return cache_.get_or_set(key, std::forward<Factory>(factory));
    }

    // peek() is read-only — shared lock is sufficient.
    [[nodiscard]] std::optional<Value> peek(const Key& key) const {
        std::shared_lock lock(mutex_);
        return cache_.peek(key);
    }

    // contains() is read-only — shared lock is sufficient.
    [[nodiscard]] bool contains(const Key& key) const {
        std::shared_lock lock(mutex_);
        return cache_.contains(key);
    }

    bool erase(const Key& key) {
        std::unique_lock lock(mutex_);
        return cache_.erase(key);
    }

    void clear() {
        std::unique_lock lock(mutex_);
        cache_.clear();
    }

    [[nodiscard]] size_type size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }

    [[nodiscard]] size_type capacity() const noexcept {
        return cache_.capacity();
    }

    [[nodiscard]] bool empty() const {
        std::shared_lock lock(mutex_);
        return cache_.empty();
    }

    [[nodiscard]] lru::CacheStats stats() const {
        std::shared_lock lock(mutex_);
        return cache_.stats();
    }

    void reset_stats() {
        std::unique_lock lock(mutex_);
        cache_.reset_stats();
    }

private:
    Cache<Key, Value>         cache_;
    mutable std::shared_mutex mutex_;
};

} // namespace lfu
