#pragma once

#include <functional>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace lru {

// Snapshot of cache access counters since construction or last reset_stats().
struct CacheStats {
    std::size_t hits        = 0;
    std::size_t misses      = 0;
    std::size_t evictions   = 0;  // LRU capacity-based evictions
    std::size_t expirations = 0;  // TTL-based removals (TtlCache only)
    std::size_t puts        = 0;

    // Fraction of lookups that found a live value; 0.0 if no lookups yet.
    [[nodiscard]] double hit_rate() const noexcept {
        const auto total = hits + misses;
        return total == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(total);
    }
};

// LRU Cache with O(1) get and put via doubly-linked list + hash map.
// Key must be hashable, Value must be copy-constructible.
template <typename Key, typename Value>
class Cache {
public:
    using key_type   = Key;
    using value_type = Value;
    using size_type  = std::size_t;

    explicit Cache(size_type capacity)
        : capacity_(capacity)
    {
        if (capacity == 0)
            throw std::invalid_argument("Cache capacity must be greater than zero");
        map_.reserve(capacity);
    }

    // Non-copyable, movable
    Cache(const Cache&)            = delete;
    Cache& operator=(const Cache&) = delete;
    Cache(Cache&&)                 = default;
    Cache& operator=(Cache&&)      = default;

    // Returns the value for key, or std::nullopt if not present.
    // Marks the entry as most-recently used on hit.
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end()) {
            ++stats_.misses;
            return std::nullopt;
        }

        ++stats_.hits;
        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    // Returns the cached value for key if present; otherwise calls factory(),
    // stores the produced value, and returns it. The factory is invoked at most once.
    // A hit increments stats.hits; a miss increments stats.misses and stats.puts.
    template <typename Factory>
    Value get_or_set(const Key& key, Factory&& factory) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            ++stats_.hits;
            order_.splice(order_.begin(), order_, it->second);
            return it->second->second;
        }
        ++stats_.misses;
        Value val = std::invoke(std::forward<Factory>(factory));
        ++stats_.puts;
        if (map_.size() == capacity_)
            evict();
        order_.emplace_front(key, val);
        map_.emplace(key, order_.begin());
        return val;
    }

    // Inserts or updates key/value. Evicts the LRU entry when over capacity.
    void put(const Key& key, Value value) {
        ++stats_.puts;
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }

        if (map_.size() == capacity_)
            evict();

        order_.emplace_front(key, std::move(value));
        map_.emplace(key, order_.begin());
    }

    // Returns the value for key without updating recency or access statistics.
    // Use this when you need to inspect a cached value but must not affect
    // the eviction order (e.g. monitoring, serialisation, read-only probes).
    [[nodiscard]] std::optional<Value> peek(const Key& key) const {
        auto it = map_.find(key);
        if (it == map_.end())
            return std::nullopt;
        return it->second->second;
    }

    // Returns true if key is present (does not update recency or stats).
    [[nodiscard]] bool contains(const Key& key) const {
        return map_.count(key) != 0;
    }

    // Removes a specific key. Returns true if the key existed.
    bool erase(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end())
            return false;
        order_.erase(it->second);
        map_.erase(it);
        return true;
    }

    void clear() noexcept {
        order_.clear();
        map_.clear();
    }

    // Adjusts capacity to new_capacity. If new_capacity < size(), LRU entries
    // are evicted (with eviction stats updated) until the size fits.
    void resize(size_type new_capacity) {
        if (new_capacity == 0)
            throw std::invalid_argument("Cache capacity must be greater than zero");
        while (map_.size() > new_capacity)
            evict();
        capacity_ = new_capacity;
        map_.reserve(new_capacity);
    }

    [[nodiscard]] size_type  size()     const noexcept { return map_.size(); }
    [[nodiscard]] size_type  capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool       empty()    const noexcept { return map_.empty(); }

    // Returns a snapshot of access statistics accumulated so far.
    [[nodiscard]] CacheStats stats()    const noexcept { return stats_; }

    // Resets all counters to zero.
    void reset_stats() noexcept { stats_ = {}; }

private:
    using ListEntry  = std::pair<Key, Value>;
    using ListType   = std::list<ListEntry>;
    using ListIter   = typename ListType::iterator;
    using MapType    = std::unordered_map<Key, ListIter>;

    void evict() {
        ++stats_.evictions;
        map_.erase(order_.back().first);
        order_.pop_back();
    }

    size_type  capacity_;
    ListType   order_;   // front = MRU, back = LRU
    MapType    map_;
    CacheStats stats_;
};

} // namespace lru
