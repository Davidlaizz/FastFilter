#ifndef NEWFILTER256_COMPACT_COMMON_HPP
#define NEWFILTER256_COMPACT_COMMON_HPP

#include <array>
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace newfilter256_compact {

using u64 = uint64_t;

struct Hash256 {
    std::array<u64, 4> words{};
};

inline auto split_segments(const Hash256 &value) -> std::array<u64, 4> {
    return value.words;
}

inline auto popcount64(u64 value) -> uint32_t {
#if defined(_MSC_VER)
    return static_cast<uint32_t>(__popcnt64(value));
#else
    return static_cast<uint32_t>(__builtin_popcountll(value));
#endif
}

} // namespace newfilter256_compact

#endif
