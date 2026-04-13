#ifndef NEWFILTER256_PREFIX_FILTER_256_HPP
#define NEWFILTER256_PREFIX_FILTER_256_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class PrefixFilter256 {
public:
    struct Hash256 {
        std::array<uint64_t, 4> words{};
    };

private:
    static constexpr size_t kWays = 4;
    static constexpr size_t kBucketCap = 16;
    static constexpr size_t kBinBits = 21;
    static constexpr size_t kFpBits = 12;
    static constexpr uint32_t kBinMask = (1u << kBinBits) - 1u;
    static constexpr uint16_t kFpMask = (1u << kFpBits) - 1u;
    static constexpr int kMaxHamming = 3;

    struct Slot {
        uint16_t fp = 0;
        uint32_t id = 0;
    };

    struct Bucket16 {
        std::array<Slot, kBucketCap> slots{};
        uint8_t size = 0;
    };

    struct SegmentMeta {
        uint32_t bin = 0;
        uint16_t fp = 0;
    };

    struct Record {
        Hash256 full{};
        std::array<uint64_t, kWays> segments{};
        uint32_t id = 0;
    };

    struct Decision {
        bool similar = false;
        bool had_candidates = false;
        uint8_t max_exact_match = 0;
        uint8_t dup_match = 0;
    };

    std::array<std::unordered_map<uint32_t, Bucket16>, kWays> l1_{};
    std::array<std::unordered_map<uint32_t, std::vector<Slot>>, kWays> l2_{};
    std::vector<Record> records_{};

    mutable std::vector<uint32_t> seen_epoch_{};
    mutable uint32_t seen_token_ = 1;

    size_t add_attempts_ = 0;
    size_t logical_items_ = 0;
    size_t l1_slots_used_ = 0;
    size_t l2_slots_used_ = 0;
    size_t insert_zero_segment_ = 0;
    size_t insert_after_match1_ = 0;
    size_t insert_after_match2_ = 0;
    size_t insert_after_match3_ = 0;
    size_t dup_match1_ = 0;
    size_t dup_match2_ = 0;
    size_t dup_match3_ = 0;
    size_t dup_match4_ = 0;

    inline static auto splitmix64(uint64_t x) -> uint64_t {
        uint64_t z = x + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27u)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31u);
    }

    inline static auto vbmi_mix(uint64_t segment, uint64_t salt) -> uint64_t {
#if defined(__AVX512VBMI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        alignas(64) static constexpr uint8_t kShuffleIndex[64] = {
            7, 6, 5, 4, 3, 2, 1, 0,
            15, 14, 13, 12, 11, 10, 9, 8,
            23, 22, 21, 20, 19, 18, 17, 16,
            31, 30, 29, 28, 27, 26, 25, 24,
            39, 38, 37, 36, 35, 34, 33, 32,
            47, 46, 45, 44, 43, 42, 41, 40,
            55, 54, 53, 52, 51, 50, 49, 48,
            63, 62, 61, 60, 59, 58, 57, 56
        };
        const __m512i idx = _mm512_load_si512((const void *) kShuffleIndex);
        const __m512i lanes = _mm512_set1_epi64(static_cast<long long>(segment ^ salt));
        const __m512i shuffled = _mm512_permutexvar_epi8(idx, lanes);
        alignas(64) uint64_t tmp[8];
        _mm512_store_si512((void *) tmp, shuffled);
        uint64_t mixed = tmp[0] ^ tmp[2] ^ tmp[5] ^ tmp[7] ^ salt;
        return splitmix64(mixed ^ segment);
#else
        return splitmix64(segment ^ salt);
#endif
    }

    inline static auto build_meta(uint64_t segment, size_t way) -> SegmentMeta {
        static constexpr std::array<uint64_t, kWays> kSalts = {
            0x243F6A8885A308D3ULL,
            0x13198A2E03707344ULL,
            0xA4093822299F31D0ULL,
            0x082EFA98EC4E6C89ULL
        };
        const uint64_t mixed = vbmi_mix(segment, kSalts[way]);
        const uint32_t bin = static_cast<uint32_t>(mixed & kBinMask);
        uint16_t fp = static_cast<uint16_t>((mixed >> kBinBits) & kFpMask);
        if (fp == 0) {
            fp = 1;
        }
        return SegmentMeta{bin, fp};
    }

    inline static auto hamming256(const Hash256 &a, const Hash256 &b) -> int {
        return __builtin_popcountll(a.words[0] ^ b.words[0]) +
               __builtin_popcountll(a.words[1] ^ b.words[1]) +
               __builtin_popcountll(a.words[2] ^ b.words[2]) +
               __builtin_popcountll(a.words[3] ^ b.words[3]);
    }

    inline static auto split_segments(const Hash256 &value) -> std::array<uint64_t, kWays> {
        return value.words;
    }

    inline void ensure_seen_size() const {
        if (seen_epoch_.size() < records_.size()) {
            seen_epoch_.resize(records_.size(), 0);
        }
    }

    inline void bump_epoch() const {
        ++seen_token_;
        if (seen_token_ == 0) {
            std::fill(seen_epoch_.begin(), seen_epoch_.end(), 0);
            seen_token_ = 1;
        }
    }

    inline void collect_from_l1_bucket(const Bucket16 &bucket, uint16_t fp, std::vector<uint32_t> *out) const {
#if defined(__AVX512BW__) && defined(__AVX512F__)
        alignas(64) uint16_t fps[32] = {0};
        for (size_t i = 0; i < bucket.size; ++i) {
            fps[i] = bucket.slots[i].fp;
        }
        const __m512i values = _mm512_load_si512((const void *) fps);
        const __m512i needle = _mm512_set1_epi16(static_cast<short>(fp));
        const __mmask32 mask = _mm512_cmpeq_epi16_mask(values, needle);
        for (size_t i = 0; i < bucket.size; ++i) {
            if (mask & (1u << i)) {
                out->push_back(bucket.slots[i].id);
            }
        }
#else
        for (size_t i = 0; i < bucket.size; ++i) {
            if (bucket.slots[i].fp == fp) {
                out->push_back(bucket.slots[i].id);
            }
        }
#endif
    }

    inline void collect_candidates(const std::array<SegmentMeta, kWays> &metas, std::vector<uint32_t> *candidates, bool *had_candidates) const {
        ensure_seen_size();
        bump_epoch();
        candidates->clear();

        for (size_t way = 0; way < kWays; ++way) {
            const auto &meta = metas[way];
            auto l1_it = l1_[way].find(meta.bin);
            if (l1_it != l1_[way].end()) {
                *had_candidates = true;
                std::vector<uint32_t> hit_ids;
                hit_ids.reserve(l1_it->second.size);
                collect_from_l1_bucket(l1_it->second, meta.fp, &hit_ids);
                for (uint32_t id : hit_ids) {
                    if (seen_epoch_[id] != seen_token_) {
                        seen_epoch_[id] = seen_token_;
                        candidates->push_back(id);
                    }
                }
            }

            auto l2_it = l2_[way].find(meta.bin);
            if (l2_it != l2_[way].end()) {
                *had_candidates = true;
                for (const auto &slot : l2_it->second) {
                    if (slot.fp != meta.fp) {
                        continue;
                    }
                    if (seen_epoch_[slot.id] != seen_token_) {
                        seen_epoch_[slot.id] = seen_token_;
                        candidates->push_back(slot.id);
                    }
                }
            }
        }
    }

    inline auto evaluate_similarity(const Hash256 &item, const std::array<uint64_t, kWays> &segments,
                                    const std::array<SegmentMeta, kWays> &metas) const -> Decision {
        Decision decision{};
        if (records_.empty()) {
            return decision;
        }

        std::vector<uint32_t> candidates;
        collect_candidates(metas, &candidates, &decision.had_candidates);
        if (candidates.empty()) {
            decision.had_candidates = false;
            return decision;
        }

        for (uint32_t id : candidates) {
            const Record &record = records_[id];
            uint8_t exact_match = 0;
            for (size_t way = 0; way < kWays; ++way) {
                exact_match += (segments[way] == record.segments[way]);
            }
            if (exact_match == 0) {
                continue;
            }
            if (exact_match > decision.max_exact_match) {
                decision.max_exact_match = exact_match;
            }
            if (exact_match == 4) {
                decision.similar = true;
                decision.dup_match = exact_match;
                return decision;
            }

            if (hamming256(item, record.full) <= kMaxHamming) {
                decision.similar = true;
                decision.dup_match = exact_match;
                return decision;
            }
        }
        return decision;
    }

    inline void insert_segment(size_t way, const SegmentMeta &meta, uint32_t id) {
        auto &bucket = l1_[way][meta.bin];
        if (bucket.size < kBucketCap) {
            bucket.slots[bucket.size++] = Slot{meta.fp, id};
            ++l1_slots_used_;
            return;
        }
        l2_[way][meta.bin].push_back(Slot{meta.fp, id});
        ++l2_slots_used_;
    }

public:
    explicit PrefixFilter256(size_t expected_items = 0) {
        if (expected_items == 0) {
            return;
        }
        const size_t reserve_each_way = std::max<size_t>(1, expected_items / 8);
        for (size_t way = 0; way < kWays; ++way) {
            l1_[way].reserve(reserve_each_way);
            l2_[way].reserve(reserve_each_way / 4);
        }
        records_.reserve(expected_items);
    }

    inline auto Add(const Hash256 &item) -> bool {
        ++add_attempts_;
        const auto segments = split_segments(item);
        std::array<SegmentMeta, kWays> metas{};
        for (size_t way = 0; way < kWays; ++way) {
            metas[way] = build_meta(segments[way], way);
        }

        const Decision decision = evaluate_similarity(item, segments, metas);
        if (!decision.had_candidates) {
            ++insert_zero_segment_;
        } else if (decision.similar) {
            switch (decision.dup_match) {
                case 1: ++dup_match1_; break;
                case 2: ++dup_match2_; break;
                case 3: ++dup_match3_; break;
                case 4: ++dup_match4_; break;
                default: break;
            }
            return false;
        } else {
            switch (decision.max_exact_match) {
                case 1: ++insert_after_match1_; break;
                case 2: ++insert_after_match2_; break;
                case 3: ++insert_after_match3_; break;
                default: break;
            }
        }

        const uint32_t id = static_cast<uint32_t>(records_.size());
        records_.push_back(Record{item, segments, id});
        for (size_t way = 0; way < kWays; ++way) {
            insert_segment(way, metas[way], id);
        }
        ++logical_items_;
        return true;
    }

    inline auto Find(const Hash256 &item) const -> bool {
        const auto segments = split_segments(item);
        std::array<SegmentMeta, kWays> metas{};
        for (size_t way = 0; way < kWays; ++way) {
            metas[way] = build_meta(segments[way], way);
        }
        return evaluate_similarity(item, segments, metas).similar;
    }

    auto get_name() const -> std::string {
        return "PF256-4x64-bin21-fp12";
    }

    auto get_add_attempts() const -> size_t {
        return add_attempts_;
    }

    auto get_logical_items() const -> size_t {
        return logical_items_;
    }

    auto get_l1_slots_used() const -> size_t {
        return l1_slots_used_;
    }

    auto get_l2_slots_used() const -> size_t {
        return l2_slots_used_;
    }

    auto get_total_occupied_slots() const -> size_t {
        return l1_slots_used_ + l2_slots_used_;
    }

    auto get_total_slot_capacity() const -> size_t {
        return kWays * (static_cast<size_t>(1u) << kBinBits) * kBucketCap;
    }

    auto get_slot_occupancy_ratio() const -> double {
        const double cap = static_cast<double>(get_total_slot_capacity());
        if (cap == 0.0) {
            return 0.0;
        }
        return static_cast<double>(get_total_occupied_slots()) / cap;
    }

    auto get_insert_zero_segment() const -> size_t {
        return insert_zero_segment_;
    }

    auto get_insert_after_match1() const -> size_t {
        return insert_after_match1_;
    }

    auto get_insert_after_match2() const -> size_t {
        return insert_after_match2_;
    }

    auto get_insert_after_match3() const -> size_t {
        return insert_after_match3_;
    }

    auto get_dup_match1() const -> size_t {
        return dup_match1_;
    }

    auto get_dup_match2() const -> size_t {
        return dup_match2_;
    }

    auto get_dup_match3() const -> size_t {
        return dup_match3_;
    }

    auto get_dup_match4() const -> size_t {
        return dup_match4_;
    }

    auto get_byte_size() const -> size_t {
        size_t bytes = 0;
        bytes += records_.capacity() * sizeof(Record);
        bytes += seen_epoch_.capacity() * sizeof(uint32_t);

        for (size_t way = 0; way < kWays; ++way) {
            bytes += l1_[way].size() * (sizeof(uint32_t) + sizeof(Bucket16));
            bytes += l2_[way].size() * (sizeof(uint32_t) + sizeof(std::vector<Slot>));
            for (const auto &kv : l2_[way]) {
                bytes += kv.second.capacity() * sizeof(Slot);
            }
        }
        return bytes;
    }
};

#endif
