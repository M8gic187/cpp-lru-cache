#pragma once

#include <chrono>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace lru {

// LRU cache with per-entry TTL (Time-To-Live).
//
// Every entry carries an expiry time point derived from the TTL supplied at
// put() time (or the default_ttl set at construction). Expired entries are
// treated as absent: get() and contains() remove them lazily on access.
// purge_expired() allows eager bulk removal of stale entries.
//
// Eviction policy: standard LRU order among non-expired entries.
// All operations remain O(1) amortised; expiry checks are a single clock
// comparison with no additional data-structure traversal per access.
//
// Template parameters:
//   Key      — must be hashable (std::unordered_map key requirements).
//   Value    — must be move-constructible.
//   Duration — any std::chrono::duration; defaults to milliseconds.
template <typename Key, typename Value,
          typename Duration = std::chrono::milliseconds>
class TtlCache {
public:
    using key_type      = Key;
    using value_type    = Value;
    using size_type     = std::size_t;
    using duration_type = Duration;
    using clock_type    = std::chrono::steady_clock;
    using time_point    = typename clock_type::time_point;

    // Constructs a cache with the given capacity and a default TTL applied
    // to every put() call that does not supply an explicit TTL.
    explicit TtlCache(size_type capacity, Duration default_ttl)
        : capacity_(capacity), default_ttl_(default_ttl)
    {
        if (capacity == 0)
            throw std::invalid_argument("Cache capacity must be greater than zero");
        map_.reserve(capacity);
    }

    TtlCache(const TtlCache&)            = delete;
    TtlCache& operator=(const TtlCache&) = delete;
    TtlCache(TtlCache&&)                 = default;
    TtlCache& operator=(TtlCache&&)      = default;

    // Returns the value for key if it exists and has not expired.
    // An expired entry is removed lazily before returning nullopt.
    // Marks a live entry as most-recently used.
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end())
            return std::nullopt;

        if (is_expired(it->second)) {
            remove(it);
            return std::nullopt;
        }

        order_.splice(order_.begin(), order_, it->second.list_it);
        return it->second.value;
    }

    // Inserts or updates key/value with the default TTL.
    void put(const Key& key, Value value) {
        put(key, std::move(value), default_ttl_);
    }

    // Inserts or updates key/value with a custom TTL.
    // An existing entry's expiry and value are both replaced.
    void put(const Key& key, Value value, Duration ttl) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second.value  = std::move(value);
            it->second.expiry = clock_type::now() + ttl;
            order_.splice(order_.begin(), order_, it->second.list_it);
            return;
        }

        if (map_.size() == capacity_)
            evict_lru();

        order_.emplace_front(key);
        map_.emplace(key, Entry{std::move(value),
                                clock_type::now() + ttl,
                                order_.begin()});
    }

    // Returns true if key is present and has not yet expired.
    // Removes an expired entry as a side effect.
    [[nodiscard]] bool contains(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end())
            return false;
        if (is_expired(it->second)) {
            remove(it);
            return false;
        }
        return true;
    }

    // Removes key regardless of expiry. Returns true if it was present.
    bool erase(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end())
            return false;
        remove(it);
        return true;
    }

    // Scans all entries and removes those whose TTL has elapsed.
    // Returns the number of entries removed.
    // Useful for periodic housekeeping to reclaim capacity proactively.
    std::size_t purge_expired() {
        std::size_t count = 0;
        auto it = map_.begin();
        while (it != map_.end()) {
            if (is_expired(it->second)) {
                order_.erase(it->second.list_it);
                it = map_.erase(it);
                ++count;
            } else {
                ++it;
            }
        }
        return count;
    }

    void clear() noexcept {
        order_.clear();
        map_.clear();
    }

    [[nodiscard]] size_type  size()        const noexcept { return map_.size(); }
    [[nodiscard]] size_type  capacity()    const noexcept { return capacity_; }
    [[nodiscard]] bool       empty()       const noexcept { return map_.empty(); }
    [[nodiscard]] Duration   default_ttl() const noexcept { return default_ttl_; }

private:
    using ListType = std::list<Key>;
    using ListIter = typename ListType::iterator;

    struct Entry {
        Value      value;
        time_point expiry;
        ListIter   list_it;
    };

    using MapType = std::unordered_map<Key, Entry>;

    [[nodiscard]] static bool is_expired(const Entry& e) noexcept {
        return clock_type::now() >= e.expiry;
    }

    void remove(typename MapType::iterator it) {
        order_.erase(it->second.list_it);
        map_.erase(it);
    }

    // Evict the LRU entry (the entry at the back of order_).
    void evict_lru() {
        const Key& lru_key = order_.back();
        map_.erase(lru_key);
        order_.pop_back();
    }

    size_type capacity_;
    Duration  default_ttl_;
    ListType  order_;   // front = MRU, back = LRU
    MapType   map_;
};

} // namespace lru
