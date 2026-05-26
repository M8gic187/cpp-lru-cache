#pragma once

#include <cstddef>

namespace lru {

// Snapshot of cache access counters since construction or last reset_stats().
// Shared by all cache variants (LRU, LFU, TtlCache).
struct CacheStats {
    std::size_t hits        = 0;
    std::size_t misses      = 0;
    std::size_t evictions   = 0;  // capacity-based evictions
    std::size_t expirations = 0;  // TTL-based removals (TtlCache only)
    std::size_t puts        = 0;

    // Fraction of lookups that found a live value; 0.0 if no lookups yet.
    [[nodiscard]] double hit_rate() const noexcept {
        const auto total = hits + misses;
        return total == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(total);
    }
};

} // namespace lru
