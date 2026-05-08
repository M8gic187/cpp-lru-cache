#pragma once

#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace lru {

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
        if (it == map_.end())
            return std::nullopt;

        // Move accessed entry to the front of the list (most recent)
        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    // Inserts or updates key/value. Evicts the LRU entry when over capacity.
    void put(const Key& key, Value value) {
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

    // Returns true if key is present (does not update recency).
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

    [[nodiscard]] size_type size()     const noexcept { return map_.size(); }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool      empty()    const noexcept { return map_.empty(); }

private:
    using ListEntry  = std::pair<Key, Value>;
    using ListType   = std::list<ListEntry>;
    using ListIter   = typename ListType::iterator;
    using MapType    = std::unordered_map<Key, ListIter>;

    void evict() {
        map_.erase(order_.back().first);
        order_.pop_back();
    }

    size_type capacity_;
    ListType  order_;   // front = MRU, back = LRU
    MapType   map_;
};

} // namespace lru
