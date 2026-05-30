#pragma once

#include "cache_stats.hpp"

#include <deque>
#include <functional>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace lru {

// Two-Queue (2Q) Cache — pollution-resistant LRU variant (Johnson & Shasha, 1994).
//
// Splits capacity into three structures to distinguish "frequent" from "recent":
//   A1in  — FIFO queue for items seen exactly once (newcomers)
//   A1out — FIFO ghost queue holding keys (no values) of recently evicted A1in items
//   Am    — LRU queue for items seen more than once (promoted from A1out)
//
// Promotion rule: when a key in A1out is accessed via put(), the value is
// inserted directly into Am, bypassing A1in. This prevents one-time sequential
// scans from polluting the hot set.
//
// Capacity split: Am = 75 % of total capacity, A1in = 25 %.
// A1out tracks up to Am's capacity worth of ghost keys.
//
// Complexity: O(1) get and put.
template <typename Key, typename Value>
class TwoQueueCache {
public:
    using key_type   = Key;
    using value_type = Value;
    using size_type  = std::size_t;

    // capacity: total number of live values held (A1in size + Am size).
    // a1in_ratio: fraction of capacity allocated to A1in (default 0.25).
    explicit TwoQueueCache(size_type capacity, double a1in_ratio = 0.25)
        : total_capacity_(capacity)
        , a1in_capacity_(std::max<size_type>(1, static_cast<size_type>(capacity * a1in_ratio)))
        , am_capacity_(std::max<size_type>(1, capacity - a1in_capacity_))
        , a1out_capacity_(am_capacity_)
    {
        if (capacity == 0)
            throw std::invalid_argument("TwoQueueCache capacity must be greater than zero");
    }

    TwoQueueCache(const TwoQueueCache&)            = delete;
    TwoQueueCache& operator=(const TwoQueueCache&) = delete;
    TwoQueueCache(TwoQueueCache&&)                 = default;
    TwoQueueCache& operator=(TwoQueueCache&&)      = default;

    // Returns the value for key, or std::nullopt on miss.
    // Items in Am are promoted to MRU; items in A1in are returned without
    // reordering (FIFO semantics preserve their original insertion position).
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        // Check Am first (frequent items)
        auto am_it = am_map_.find(key);
        if (am_it != am_map_.end()) {
            ++stats_.hits;
            am_list_.splice(am_list_.begin(), am_list_, am_it->second);
            return am_it->second->second;
        }
        // Check A1in (recent items)
        auto a1_it = a1in_map_.find(key);
        if (a1_it != a1in_map_.end()) {
            ++stats_.hits;
            return a1_it->second->second;
        }
        ++stats_.misses;
        return std::nullopt;
    }

    // Returns the value for key without updating recency or statistics.
    [[nodiscard]] std::optional<Value> peek(const Key& key) const {
        auto am_it = am_map_.find(key);
        if (am_it != am_map_.end())
            return am_it->second->second;
        auto a1_it = a1in_map_.find(key);
        if (a1_it != a1in_map_.end())
            return a1_it->second->second;
        return std::nullopt;
    }

    // Inserts or updates key/value, applying the 2Q promotion rules.
    void put(const Key& key, Value value) {
        ++stats_.puts;

        // Key already in Am → update and move to MRU
        auto am_it = am_map_.find(key);
        if (am_it != am_map_.end()) {
            am_it->second->second = std::move(value);
            am_list_.splice(am_list_.begin(), am_list_, am_it->second);
            return;
        }

        // Key already in A1in → update in place (FIFO, no reordering)
        auto a1_it = a1in_map_.find(key);
        if (a1_it != a1in_map_.end()) {
            a1_it->second->second = std::move(value);
            return;
        }

        // Key was recently evicted from A1in → promote directly to Am
        if (a1out_set_.count(key)) {
            remove_from_a1out(key);
            insert_into_am(key, std::move(value));
            return;
        }

        // Completely new key → insert into A1in
        insert_into_a1in(key, std::move(value));
    }

    // Returns the value for key if present; otherwise calls factory(), stores
    // the result, and returns it. A hit does not call factory.
    template <typename Factory>
    Value get_or_set(const Key& key, Factory&& factory) {
        // Check Am
        auto am_it = am_map_.find(key);
        if (am_it != am_map_.end()) {
            ++stats_.hits;
            am_list_.splice(am_list_.begin(), am_list_, am_it->second);
            return am_it->second->second;
        }
        // Check A1in
        auto a1_it = a1in_map_.find(key);
        if (a1_it != a1in_map_.end()) {
            ++stats_.hits;
            return a1_it->second->second;
        }
        ++stats_.misses;
        Value val = std::invoke(std::forward<Factory>(factory));
        ++stats_.puts;
        if (a1out_set_.count(key)) {
            remove_from_a1out(key);
            insert_into_am(key, val);
        } else {
            insert_into_a1in(key, val);
        }
        return val;
    }

    // Returns true if key is present in A1in or Am (does not update statistics).
    [[nodiscard]] bool contains(const Key& key) const {
        return am_map_.count(key) || a1in_map_.count(key);
    }

    // Removes key from the cache. Returns true if the key existed.
    bool erase(const Key& key) {
        auto am_it = am_map_.find(key);
        if (am_it != am_map_.end()) {
            am_list_.erase(am_it->second);
            am_map_.erase(am_it);
            return true;
        }
        auto a1_it = a1in_map_.find(key);
        if (a1_it != a1in_map_.end()) {
            a1in_list_.erase(a1_it->second);
            a1in_map_.erase(a1_it);
            return true;
        }
        return false;
    }

    // Removes all entries (live and ghost).
    void clear() noexcept {
        am_list_.clear();   am_map_.clear();
        a1in_list_.clear(); a1in_map_.clear();
        a1out_deque_.clear(); a1out_set_.clear();
    }

    // Total number of live entries (A1in + Am).
    [[nodiscard]] size_type size()          const noexcept { return am_map_.size() + a1in_map_.size(); }
    [[nodiscard]] size_type capacity()      const noexcept { return total_capacity_; }
    [[nodiscard]] bool      empty()         const noexcept { return am_map_.empty() && a1in_map_.empty(); }

    // Sub-queue sizes (useful for diagnostics).
    [[nodiscard]] size_type am_size()       const noexcept { return am_map_.size(); }
    [[nodiscard]] size_type a1in_size()     const noexcept { return a1in_map_.size(); }
    [[nodiscard]] size_type a1out_size()    const noexcept { return a1out_set_.size(); }
    [[nodiscard]] size_type am_capacity()   const noexcept { return am_capacity_; }
    [[nodiscard]] size_type a1in_capacity() const noexcept { return a1in_capacity_; }

    [[nodiscard]] CacheStats stats()        const noexcept { return stats_; }
    void reset_stats()                            noexcept { stats_ = {}; }

private:
    using ListEntry = std::pair<Key, Value>;
    using ListType  = std::list<ListEntry>;
    using ListIter  = typename ListType::iterator;

    void insert_into_am(const Key& key, Value value) {
        if (am_map_.size() == am_capacity_)
            evict_from_am();
        am_list_.emplace_front(key, std::move(value));
        am_map_.emplace(key, am_list_.begin());
    }

    void evict_from_am() {
        ++stats_.evictions;
        am_map_.erase(am_list_.back().first);
        am_list_.pop_back();
    }

    void insert_into_a1in(const Key& key, Value value) {
        if (a1in_map_.size() == a1in_capacity_)
            demote_from_a1in();
        a1in_list_.emplace_front(key, std::move(value));
        a1in_map_.emplace(key, a1in_list_.begin());
    }

    // Move the oldest A1in entry to the ghost queue (drop its value).
    void demote_from_a1in() {
        ++stats_.evictions;
        const Key& lru_key = a1in_list_.back().first;
        if (a1out_set_.size() == a1out_capacity_)
            drop_oldest_ghost();
        a1out_deque_.push_back(lru_key);
        a1out_set_.insert(lru_key);
        a1in_map_.erase(lru_key);
        a1in_list_.pop_back();
    }

    void drop_oldest_ghost() {
        a1out_set_.erase(a1out_deque_.front());
        a1out_deque_.pop_front();
    }

    void remove_from_a1out(const Key& key) {
        a1out_set_.erase(key);
        // Lazy removal from deque: leave ghost key in place; it will be skipped
        // or dropped naturally when a1out_capacity_ is reached. To avoid linear
        // scans we rely on a1out_set_ as the authoritative membership test.
        // The deque may thus contain stale keys; drop_oldest_ghost() skips them.
        //
        // However we must not let the deque grow unboundedly. Purge stale front
        // entries now so size stays bounded by a1out_capacity_.
        while (!a1out_deque_.empty() && !a1out_set_.count(a1out_deque_.front()))
            a1out_deque_.pop_front();
    }

    size_type total_capacity_;
    size_type a1in_capacity_;
    size_type am_capacity_;
    size_type a1out_capacity_;

    // Am: LRU queue of frequent items (front = MRU)
    ListType am_list_;
    std::unordered_map<Key, ListIter> am_map_;

    // A1in: FIFO queue of recent items (front = newest)
    ListType a1in_list_;
    std::unordered_map<Key, ListIter> a1in_map_;

    // A1out: FIFO ghost queue of keys evicted from A1in
    std::deque<Key>          a1out_deque_;
    std::unordered_set<Key>  a1out_set_;

    CacheStats stats_;
};

} // namespace lru
