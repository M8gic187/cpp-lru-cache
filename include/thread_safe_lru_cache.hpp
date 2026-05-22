#pragma once

#include "lru_cache.hpp"

#include <mutex>
#include <optional>
#include <shared_mutex>

namespace lru {

// Thread-safe wrapper around Cache<K,V>.
// Concurrent reads (get/contains) share a read lock.
// Writes (put/erase/clear) acquire an exclusive lock.
template <typename Key, typename Value>
class ThreadSafeCache {
public:
    using size_type = typename Cache<Key, Value>::size_type;

    explicit ThreadSafeCache(size_type capacity)
        : cache_(capacity) {}

    ThreadSafeCache(const ThreadSafeCache&)            = delete;
    ThreadSafeCache& operator=(const ThreadSafeCache&) = delete;

    // get promotes the entry to MRU — needs exclusive lock despite appearing read-like.
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        std::unique_lock lock(mutex_);
        return cache_.get(key);
    }

    void put(const Key& key, Value value) {
        std::unique_lock lock(mutex_);
        cache_.put(key, std::move(value));
    }

    // Thread-safe get_or_set: holds the exclusive lock for the full lookup+insert.
    // Prevents duplicate factory invocations under concurrent access for the same key.
    template <typename Factory>
    Value get_or_set(const Key& key, Factory&& factory) {
        std::unique_lock lock(mutex_);
        return cache_.get_or_set(key, std::forward<Factory>(factory));
    }

    // Adjusts cache capacity, evicting LRU entries if new_capacity < size().
    void resize(size_type new_capacity) {
        std::unique_lock lock(mutex_);
        cache_.resize(new_capacity);
    }

    // contains does not change internal order — shared lock is sufficient.
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

    [[nodiscard]] size_type capacity() const {
        std::shared_lock lock(mutex_);
        return cache_.capacity();
    }

    [[nodiscard]] bool empty() const {
        std::shared_lock lock(mutex_);
        return cache_.empty();
    }

    // Returns a snapshot of access statistics (hits, misses, evictions, puts).
    [[nodiscard]] CacheStats stats() const {
        std::shared_lock lock(mutex_);
        return cache_.stats();
    }

    // Resets all counters to zero.
    void reset_stats() {
        std::unique_lock lock(mutex_);
        cache_.reset_stats();
    }

private:
    Cache<Key, Value>        cache_;
    mutable std::shared_mutex mutex_;
};

} // namespace lru
