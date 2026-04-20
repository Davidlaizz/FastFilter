#ifndef NEWFILTER256_COMPACT_FILTER_LAYER_HPP
#define NEWFILTER256_COMPACT_FILTER_LAYER_HPP

#include "common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace newfilter256_compact {

struct SegmentMeta {
    uint32_t bin = 0;
    uint16_t fp = 0;
};

struct CandidateEntry {
    uint32_t id = 0;
    uint8_t filter_hit_count = 0;
    uint8_t filter_hit_mask = 0;
};

struct FilterCollectSummary {
    size_t candidate_count = 0;
    uint8_t max_filter_hits = 0;
    size_t l1_way_hits = 0;
    size_t l2_way_hits = 0;
    size_t l2_way_probes = 0;
};

class CompactFilterLayer {
public:
    static constexpr size_t kWays = 4;
    static constexpr size_t kBucketCap = 16;
    static constexpr size_t kBinBits = 20;
    static constexpr size_t kFpBits = 12;
    static constexpr uint32_t kBucketCount = (1u << kBinBits);
    static constexpr uint32_t kBinMask = kBucketCount - 1u;
    static constexpr uint16_t kFpMask = (1u << kFpBits) - 1u;

private:
    static constexpr int32_t kInvalidIndex = -1;
    static constexpr size_t kPackedSlotBytes = 5; // id24 + fp12 + 4bit pad
    static constexpr uint32_t kMaxPackedId = 0xFFFFFFu;
    static constexpr size_t kL2PageSlots = 32;

    struct L2Page {
        int32_t next = kInvalidIndex;
        uint16_t size = 0;
        std::array<uint8_t, kL2PageSlots * kPackedSlotBytes> data{};
    };

    struct WayStorage {
        std::vector<uint8_t> l1_sizes{};
        std::vector<uint8_t> l1_slots{};
        std::vector<int32_t> l2_heads{};
        std::vector<L2Page> l2_pages{};
        size_t l1_slots_used = 0;
        size_t l2_slots_used = 0;
    };

    std::array<WayStorage, kWays> ways_{};

    mutable std::vector<uint32_t> seen_epoch_{};
    mutable std::vector<uint32_t> seen_index_{};
    mutable std::vector<uint8_t> seen_counts_{};
    mutable std::vector<uint8_t> seen_masks_{};
    mutable uint32_t epoch_token_ = 1;

    size_t collect_calls_ = 0;
    size_t collect_zero_hit_calls_ = 0;
    size_t total_candidates_returned_ = 0;
    std::array<size_t, 5> collect_max_hit_hist_{{0, 0, 0, 0, 0}};
    size_t l1_way_probes_ = 0;
    size_t l1_way_hits_ = 0;
    size_t l2_way_probes_ = 0;
    size_t l2_way_hits_ = 0;

    inline static auto splitmix64(uint64_t value) -> uint64_t {
        uint64_t z = value + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27u)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31u);
    }

    inline static auto mix_segment(uint64_t segment, size_t way) -> uint64_t {
        static constexpr std::array<uint64_t, kWays> kSalts = {
            0x243F6A8885A308D3ULL,
            0x13198A2E03707344ULL,
            0xA4093822299F31D0ULL,
            0x082EFA98EC4E6C89ULL
        };
        return splitmix64(segment ^ kSalts[way]);
    }

    inline static auto meta_from_segment(uint64_t segment, size_t way) -> SegmentMeta {
        const uint64_t mixed = mix_segment(segment, way);
        const uint32_t bin = static_cast<uint32_t>(mixed & kBinMask);
        uint16_t fp = static_cast<uint16_t>((mixed >> kBinBits) & kFpMask);
        if (fp == 0) {
            fp = 1;
        }
        return SegmentMeta{bin, fp};
    }

    inline auto bucket_offset(uint32_t bin) const -> size_t {
        return static_cast<size_t>(bin) * kBucketCap * kPackedSlotBytes;
    }

    inline static void write_packed_slot(uint8_t *dst, uint32_t id, uint16_t fp) {
        dst[0] = static_cast<uint8_t>(id & 0xFFu);
        dst[1] = static_cast<uint8_t>((id >> 8u) & 0xFFu);
        dst[2] = static_cast<uint8_t>((id >> 16u) & 0xFFu);
        dst[3] = static_cast<uint8_t>(fp & 0xFFu);
        dst[4] = static_cast<uint8_t>((fp >> 8u) & 0x0Fu);
    }

    inline static auto read_packed_id(const uint8_t *src) -> uint32_t {
        return static_cast<uint32_t>(src[0]) |
               (static_cast<uint32_t>(src[1]) << 8u) |
               (static_cast<uint32_t>(src[2]) << 16u);
    }

    inline static auto read_packed_fp(const uint8_t *src) -> uint16_t {
        return static_cast<uint16_t>(static_cast<uint16_t>(src[3]) |
                                     (static_cast<uint16_t>(src[4] & 0x0Fu) << 8u));
    }

    inline static auto page_slot_ptr(L2Page *page, size_t idx) -> uint8_t * {
        return &page->data[idx * kPackedSlotBytes];
    }

    inline static auto page_slot_ptr(const L2Page *page, size_t idx) -> const uint8_t * {
        return &page->data[idx * kPackedSlotBytes];
    }

    inline void ensure_scratch_size(size_t logical_items) const {
        if (seen_epoch_.size() >= logical_items) {
            return;
        }
        seen_epoch_.resize(logical_items, 0);
        seen_index_.resize(logical_items, 0);
        seen_counts_.resize(logical_items, 0);
        seen_masks_.resize(logical_items, 0);
    }

    inline void bump_epoch() const {
        ++epoch_token_;
        if (epoch_token_ != 0) {
            return;
        }
        std::fill(seen_epoch_.begin(), seen_epoch_.end(), 0);
        epoch_token_ = 1;
    }

    inline void mark_candidate(uint32_t id, uint8_t way_bit, std::vector<CandidateEntry> *out, uint8_t *max_hits) const {
        if (seen_epoch_[id] != epoch_token_) {
            seen_epoch_[id] = epoch_token_;
            seen_counts_[id] = 1;
            seen_masks_[id] = way_bit;
            seen_index_[id] = static_cast<uint32_t>(out->size());
            out->push_back(CandidateEntry{id, 1, way_bit});
            if (*max_hits < 1) {
                *max_hits = 1;
            }
            return;
        }
        const uint32_t index = seen_index_[id];
        if ((seen_masks_[id] & way_bit) != 0) {
            return;
        }
        seen_masks_[id] = static_cast<uint8_t>(seen_masks_[id] | way_bit);
        seen_counts_[id] = static_cast<uint8_t>(seen_counts_[id] + 1);
        out->at(index).filter_hit_mask = seen_masks_[id];
        out->at(index).filter_hit_count = seen_counts_[id];
        if (*max_hits < out->at(index).filter_hit_count) {
            *max_hits = out->at(index).filter_hit_count;
        }
    }

    inline void record_collect_stats(const FilterCollectSummary &summary) {
        ++collect_calls_;
        total_candidates_returned_ += summary.candidate_count;
        l1_way_probes_ += kWays;
        l1_way_hits_ += summary.l1_way_hits;
        l2_way_probes_ += summary.l2_way_probes;
        l2_way_hits_ += summary.l2_way_hits;
        if (summary.candidate_count == 0) {
            ++collect_zero_hit_calls_;
        }
        ++collect_max_hit_hist_[summary.max_filter_hits];
    }

    inline static void check_id_range(uint32_t id) {
        if (id > kMaxPackedId) {
            throw std::runtime_error("CompactFilterLayer id overflow: id exceeds 24-bit packing range");
        }
    }

public:
    explicit CompactFilterLayer(size_t expected_items = 0) {
        const size_t reserve_l2_each_way = std::max<size_t>(1, expected_items / 16);
        for (size_t way = 0; way < kWays; ++way) {
            ways_[way].l1_sizes.assign(kBucketCount, 0);
            ways_[way].l1_slots.assign(static_cast<size_t>(kBucketCount) * kBucketCap * kPackedSlotBytes, 0);
            ways_[way].l2_heads.assign(kBucketCount, kInvalidIndex);
            ways_[way].l2_pages.reserve(reserve_l2_each_way);
        }
    }

    inline static auto build_metas(const Hash256 &value) -> std::array<SegmentMeta, kWays> {
        const auto segments = split_segments(value);
        std::array<SegmentMeta, kWays> metas{};
        for (size_t way = 0; way < kWays; ++way) {
            metas[way] = meta_from_segment(segments[way], way);
        }
        return metas;
    }

    inline void collect_candidates(const std::array<SegmentMeta, kWays> &metas, size_t logical_items,
                                   std::vector<CandidateEntry> *out, FilterCollectSummary *summary) const {
        ensure_scratch_size(logical_items);
        bump_epoch();
        out->clear();
        *summary = FilterCollectSummary{};

        uint8_t max_hits = 0;
        for (size_t way = 0; way < kWays; ++way) {
            const auto &meta = metas[way];
            const auto &storage = ways_[way];
            const size_t base = bucket_offset(meta.bin);
            const size_t l1_size = storage.l1_sizes[meta.bin];
            bool l1_way_hit = false;
            for (size_t slot = 0; slot < l1_size; ++slot) {
                const uint8_t *entry = &storage.l1_slots[base + slot * kPackedSlotBytes];
                if (read_packed_fp(entry) != meta.fp) {
                    continue;
                }
                mark_candidate(read_packed_id(entry), static_cast<uint8_t>(1u << way), out, &max_hits);
                l1_way_hit = true;
            }
            if (l1_way_hit) {
                ++summary->l1_way_hits;
            }

            const int32_t head = storage.l2_heads[meta.bin];
            if (head == kInvalidIndex) {
                continue;
            }
            ++summary->l2_way_probes;
            bool l2_way_hit = false;
            for (int32_t page_idx = head; page_idx != kInvalidIndex; page_idx = storage.l2_pages[page_idx].next) {
                const L2Page &page = storage.l2_pages[page_idx];
                for (size_t slot = 0; slot < page.size; ++slot) {
                    const uint8_t *entry = page_slot_ptr(&page, slot);
                    if (read_packed_fp(entry) != meta.fp) {
                        continue;
                    }
                    mark_candidate(read_packed_id(entry), static_cast<uint8_t>(1u << way), out, &max_hits);
                    l2_way_hit = true;
                }
            }
            if (l2_way_hit) {
                ++summary->l2_way_hits;
            }
        }

        std::stable_sort(out->begin(), out->end(), [](const CandidateEntry &lhs, const CandidateEntry &rhs) {
            if (lhs.filter_hit_count != rhs.filter_hit_count) {
                return lhs.filter_hit_count > rhs.filter_hit_count;
            }
            return lhs.id < rhs.id;
        });

        summary->candidate_count = out->size();
        summary->max_filter_hits = max_hits;
        const_cast<CompactFilterLayer *>(this)->record_collect_stats(*summary);
    }

    inline void insert_record(uint32_t id, const std::array<SegmentMeta, kWays> &metas) {
        check_id_range(id);
        for (size_t way = 0; way < kWays; ++way) {
            const auto &meta = metas[way];
            auto &storage = ways_[way];
            const size_t base = bucket_offset(meta.bin);
            const size_t l1_size = storage.l1_sizes[meta.bin];
            if (l1_size < kBucketCap) {
                uint8_t *entry = &storage.l1_slots[base + l1_size * kPackedSlotBytes];
                write_packed_slot(entry, id, meta.fp);
                storage.l1_sizes[meta.bin] = static_cast<uint8_t>(l1_size + 1);
                ++storage.l1_slots_used;
                continue;
            }

            int32_t &head = storage.l2_heads[meta.bin];
            int32_t target_page = head;
            if (target_page == kInvalidIndex || storage.l2_pages[target_page].size >= kL2PageSlots) {
                L2Page page{};
                page.next = head;
                storage.l2_pages.push_back(page);
                head = static_cast<int32_t>(storage.l2_pages.size() - 1);
                target_page = head;
            }

            L2Page &page = storage.l2_pages[target_page];
            uint8_t *entry = page_slot_ptr(&page, page.size);
            write_packed_slot(entry, id, meta.fp);
            ++page.size;
            ++storage.l2_slots_used;
        }
    }

    auto get_l1_slots_used() const -> size_t {
        size_t total = 0;
        for (const auto &way : ways_) {
            total += way.l1_slots_used;
        }
        return total;
    }

    auto get_l2_slots_used() const -> size_t {
        size_t total = 0;
        for (const auto &way : ways_) {
            total += way.l2_slots_used;
        }
        return total;
    }

    auto get_total_occupied_slots() const -> size_t {
        return get_l1_slots_used() + get_l2_slots_used();
    }

    auto get_total_slot_capacity() const -> size_t {
        return static_cast<size_t>(kWays) * kBucketCount * kBucketCap;
    }

    auto get_slot_occupancy_ratio() const -> double {
        const double cap = static_cast<double>(get_total_slot_capacity());
        return (cap > 0.0) ? static_cast<double>(get_total_occupied_slots()) / cap : 0.0;
    }

    auto get_collect_calls() const -> size_t {
        return collect_calls_;
    }

    auto get_collect_zero_hit_calls() const -> size_t {
        return collect_zero_hit_calls_;
    }

    auto get_total_candidates_returned() const -> size_t {
        return total_candidates_returned_;
    }

    auto get_collect_max_hit_hist(size_t hit_count) const -> size_t {
        if (hit_count >= collect_max_hit_hist_.size()) {
            return 0;
        }
        return collect_max_hit_hist_[hit_count];
    }

    auto get_l1_way_probe_count() const -> size_t {
        return l1_way_probes_;
    }

    auto get_l1_way_hit_count() const -> size_t {
        return l1_way_hits_;
    }

    auto get_l2_way_probe_count() const -> size_t {
        return l2_way_probes_;
    }

    auto get_l2_way_hit_count() const -> size_t {
        return l2_way_hits_;
    }

    auto get_byte_size() const -> size_t {
        size_t bytes = 0;
        bytes += seen_epoch_.capacity() * sizeof(uint32_t);
        bytes += seen_index_.capacity() * sizeof(uint32_t);
        bytes += seen_counts_.capacity() * sizeof(uint8_t);
        bytes += seen_masks_.capacity() * sizeof(uint8_t);
        for (const auto &way : ways_) {
            bytes += way.l1_sizes.capacity() * sizeof(uint8_t);
            bytes += way.l1_slots.capacity() * sizeof(uint8_t);
            bytes += way.l2_heads.capacity() * sizeof(int32_t);
            bytes += way.l2_pages.capacity() * sizeof(L2Page);
        }
        return bytes;
    }
};

} // namespace newfilter256_compact

#endif
