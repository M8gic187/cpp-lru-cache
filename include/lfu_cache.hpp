#pragma once

#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace lfu {

// O(1) LFU (Least Frequently Used) cache with LRU tie-breaking.
//
// Evicts the entry with the lowest access frequency. Among entries that share
// the minimum frequency, the least recently used one is chosen (LRU fallback).
//
// Algorithm uses two hash maps and per-frequency doubly-linked lists:
//   key_map_  : Key  -> {Value, frequency, iterator into its freq-list}
//   freq_map_ : freq -> list<Key>  (front = MRU, back = LRU within freq)
//   min_freq_ : tracks the current minimum frequency for O(1) eviction.
//
// All operations (get, put, erase) are O(1) amortised.
// Key must be hashable; Value must be move-constructible.
template <typename Key, typename Value>
class Cache {
public:
    using key_type   = Key;
    using value_type = Value;
    using size_type  = std::size_t;
    using freq_type  = std::size_t;

    explicit Cache(size_type capacity) : capacity_(capacity) {
        if (capacity == 0)
            throw std::invalid_argument("Cache capacity must be greater than zero");
    }

    Cache(const Cache&)            = delete;
    Cache& operator=(const Cache&) = delete;
    Cache(Cache&&)                 = default;
    Cache& operator=(Cache&&)      = default;

    // Returns the value for key, or std::nullopt if not present.
    // Increments the access frequency of the entry on hit.
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        auto it = key_map_.find(key);
        if (it == key_map_.end())
            return std::nullopt;

        promote(it);
        return it->second.value;
    }

    // Inserts or updates key/value. On update, the frequency is incremented.
    // Evicts the LFU/LRU entry when the cache is at capacity.
    void put(const Key& key, Value value) {
        auto it = key_map_.find(key);
        if (it != key_map_.end()) {
            it->second.value = std::move(value);
            promote(it);
            return;
        }

        if (key_map_.size() == capacity_)
            evict();

        freq_map_[1].emplace_front(key);
        key_map_.emplace(key, Entry{std::move(value), 1, freq_map_[1].begin()});
        min_freq_ = 1;
    }

    // Returns true if key is present (does not update frequency).
    [[nodiscard]] bool contains(const Key& key) const {
        return key_map_.count(key) != 0;
    }

    // Removes key. Returns true if it was present.
    bool erase(const Key& key) {
        auto it = key_map_.find(key);
        if (it == key_map_.end())
            return false;

        remove_from_freq_list(it->second.freq, it->second.list_it);
        key_map_.erase(it);
        return true;
    }

    void clear() noexcept {
        key_map_.clear();
        freq_map_.clear();
        min_freq_ = 0;
    }

    [[nodiscard]] size_type size()     const noexcept { return key_map_.size(); }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool      empty()    const noexcept { return key_map_.empty(); }

private:
    using FreqList   = std::list<Key>;
    using FreqListIt = typename FreqList::iterator;

    struct Entry {
        Value       value;
        freq_type   freq;
        FreqListIt  list_it;
    };

    using KeyMap  = std::unordered_map<Key, Entry>;
    using FreqMap = std::unordered_map<freq_type, FreqList>;

    // Move the entry to the next frequency bucket (freq+1).
    void promote(typename KeyMap::iterator it) {
        auto& entry    = it->second;
        const auto old = entry.freq;
        const auto nxt = old + 1;

        remove_from_freq_list(old, entry.list_it);
        if (old == min_freq_ && freq_map_.find(old) == freq_map_.end())
            min_freq_ = nxt;

        freq_map_[nxt].emplace_front(it->first);
        entry.freq    = nxt;
        entry.list_it = freq_map_[nxt].begin();
    }

    // Evict the LRU entry from the minimum-frequency bucket.
    void evict() {
        auto& lru_list = freq_map_[min_freq_];
        const Key& lru_key = lru_list.back();
        key_map_.erase(lru_key);
        lru_list.pop_back();
        if (lru_list.empty())
            freq_map_.erase(min_freq_);
        // min_freq_ is reset to 1 by the caller (put always inserts with freq=1).
    }

    // Remove a key from its frequency list; erase the bucket if now empty.
    void remove_from_freq_list(freq_type freq, FreqListIt it) {
        auto& lst = freq_map_[freq];
        lst.erase(it);
        if (lst.empty())
            freq_map_.erase(freq);
    }

    size_type capacity_;
    freq_type min_freq_ = 0;
    KeyMap    key_map_;
    FreqMap   freq_map_;
};

} // namespace lfu
