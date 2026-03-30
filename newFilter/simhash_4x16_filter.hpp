#ifndef NEW_FILTER_SIMHASH_4X16_FILTER_HPP
#define NEW_FILTER_SIMHASH_4X16_FILTER_HPP

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <string>
#include <vector>

class SimHash4x16OrFilter {
    static constexpr size_t kWays = 4;
    static constexpr size_t kSegmentBits = 16;
    static constexpr size_t kBucketBits = 12;
    static constexpr size_t kFingerprintBits = 4;
    static constexpr size_t kBucketsPerWay = 1ULL << kBucketBits;
    static constexpr size_t kSlotsPerBucket = 16;
    static constexpr uint16_t kFullMask = 0xFFFFu;

    using Bucket = std::array<uint8_t, kSlotsPerBucket>;

    std::array<std::vector<Bucket>, kWays> buckets{};
    std::array<std::vector<uint16_t>, kWays> occupied_masks{};
    size_t logical_items = 0;

    __attribute__((always_inline)) inline static uint16_t segment_of(uint64_t x, size_t way) {
        return static_cast<uint16_t>(x >> (way * kSegmentBits));
    }

    __attribute__((always_inline)) inline static uint16_t segment_bin(uint16_t segment) {
        return segment >> kFingerprintBits;
    }

    __attribute__((always_inline)) inline static uint8_t segment_fp(uint16_t segment) {
        return static_cast<uint8_t>(segment & ((1u << kFingerprintBits) - 1u));
    }

    __attribute__((always_inline)) inline static bool bucket_contains(const Bucket &bucket, uint16_t occupied_mask, uint8_t fp) {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        const __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i *>(bucket.data()));
        const __m128i target = _mm_set1_epi8(static_cast<char>(fp));
        const __mmask16 cmp_mask = _mm_cmpeq_epi8_mask(data, target);
        return (cmp_mask & occupied_mask) != 0;
#else
        for (size_t i = 0; i < kSlotsPerBucket; ++i) {
            if ((occupied_mask & (1u << i)) && (bucket[i] == fp)) {
                return true;
            }
        }
        return false;
#endif
    }

    __attribute__((always_inline)) inline static bool bucket_insert(Bucket *bucket, uint16_t *occupied_mask, uint8_t fp) {
        if (bucket_contains(*bucket, *occupied_mask, fp)) {
            return false;
        }

        if (*occupied_mask == kFullMask) {
            return false;
        }

        const uint16_t free_mask = static_cast<uint16_t>(~(*occupied_mask));
        assert(free_mask);
        const unsigned slot = static_cast<unsigned>(__builtin_ctz(static_cast<unsigned>(free_mask)));
        (*bucket)[slot] = fp;
        (*occupied_mask) |= static_cast<uint16_t>(1u << slot);
        return true;
    }

public:
    explicit SimHash4x16OrFilter(size_t max_number_of_elements) {
        (void) max_number_of_elements;
        for (size_t way = 0; way < kWays; ++way) {
            buckets[way].resize(kBucketsPerWay);
            occupied_masks[way].assign(kBucketsPerWay, 0);
            for (auto &bucket : buckets[way]) {
                bucket.fill(0);
            }
        }
    }

    inline auto Find(const uint64_t &item) const -> bool {
        for (size_t way = 0; way < kWays; ++way) {
            const uint16_t segment = segment_of(item, way);
            const uint16_t bin = segment_bin(segment);
            const uint8_t fp = segment_fp(segment);
            if (bucket_contains(buckets[way][bin], occupied_masks[way][bin], fp)) {
                return true;
            }
        }
        return false;
    }

    inline void Add(const uint64_t &item) {
        for (size_t way = 0; way < kWays; ++way) {
            const uint16_t segment = segment_of(item, way);
            const uint16_t bin = segment_bin(segment);
            const uint8_t fp = segment_fp(segment);
            bucket_insert(&buckets[way][bin], &occupied_masks[way][bin], fp);
        }
        logical_items++;
    }

    auto get_name() const -> std::string {
        return "SimHash-4x16-OR";
    }

    auto get_byte_size() const -> size_t {
        const size_t bytes_per_way = kBucketsPerWay * (sizeof(Bucket) + sizeof(uint16_t));
        return kWays * bytes_per_way;
    }

    auto get_cap() const -> size_t {
        return logical_items;
    }
};

#endif
