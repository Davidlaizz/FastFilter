#ifndef NEW_FILTER_SIMHASH_4X16_FILTER_HPP
#define NEW_FILTER_SIMHASH_4X16_FILTER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class SimHash4x16OrFilter {
    static constexpr size_t kWays = 4;
    static constexpr size_t kSegmentBits = 16;
    static constexpr size_t kSegmentValues = 1ULL << kSegmentBits;
    static constexpr uint64_t kSegmentMask = 0xFFFFULL;
    static constexpr int kMaxHamming = 3;

    struct Record {
        uint64_t full_hash = 0;
        std::array<uint16_t, kWays> segments{};
        uint32_t id = 0;
    };

    std::vector<Record> records{};
    std::array<std::vector<std::vector<uint32_t>>, kWays> index{};
    std::array<size_t, kWays> occupied_segments_per_way{};
    size_t add_attempts = 0;
    size_t logical_items_any = 0;
    size_t logical_items_full = 0;

    mutable std::vector<uint32_t> seen_epoch{};
    mutable uint32_t seen_token = 1;

    __attribute__((always_inline)) inline static uint16_t segment_of(uint64_t x, size_t way) {
        return static_cast<uint16_t>(x >> (way * kSegmentBits));
    }

    __attribute__((always_inline)) inline static std::array<uint16_t, kWays> split_segments(uint64_t x) {
        std::array<uint16_t, kWays> segs{};
        for (size_t way = 0; way < kWays; ++way) {
            segs[way] = segment_of(x, way);
        }
        return segs;
    }

    inline void ensure_seen_size() const {
        if (seen_epoch.size() < records.size()) {
            seen_epoch.resize(records.size(), 0);
        }
    }

    inline void bump_epoch() const {
        seen_token++;
        if (seen_token == 0) {
            std::fill(seen_epoch.begin(), seen_epoch.end(), 0);
            seen_token = 1;
        }
    }

    inline bool is_similar(uint64_t item, const std::array<uint16_t, kWays> &segs) const {
        if (records.empty()) {
            return false;
        }

        ensure_seen_size();
        bump_epoch();
        std::vector<uint32_t> candidates;

        for (size_t way = 0; way < kWays; ++way) {
            const auto &bucket = index[way][segs[way]];
            for (uint32_t id : bucket) {
                if (seen_epoch[id] != seen_token) {
                    seen_epoch[id] = seen_token;
                    candidates.push_back(id);
                }
            }
        }

        if (candidates.empty()) {
            return false;
        }

        for (uint32_t id : candidates) {
            const Record &rec = records[id];
            uint8_t match_mask = 0;
            int match_count = 0;
            for (size_t way = 0; way < kWays; ++way) {
                if (segs[way] == rec.segments[way]) {
                    match_mask |= static_cast<uint8_t>(1u << way);
                    match_count++;
                }
            }
            if (match_count == 0) {
                continue;
            }
            if (match_count == static_cast<int>(kWays)) {
                return true;
            }

            uint64_t remaining_mask = 0;
            for (size_t way = 0; way < kWays; ++way) {
                if ((match_mask & (1u << way)) == 0) {
                    remaining_mask |= (kSegmentMask << (way * kSegmentBits));
                }
            }
            const uint64_t diff = (item ^ rec.full_hash) & remaining_mask;
            const int hamming = __builtin_popcountll(diff);
            if (hamming <= kMaxHamming) {
                return true;
            }
        }
        return false;
    }

public:
    explicit SimHash4x16OrFilter(size_t max_number_of_elements) {
        (void) max_number_of_elements;
        for (size_t way = 0; way < kWays; ++way) {
            index[way].resize(kSegmentValues);
        }
    }

    inline auto Find(const uint64_t &item) const -> bool {
        const auto segs = split_segments(item);
        return is_similar(item, segs);
    }

    inline bool Add(const uint64_t &item) {
        add_attempts++;
        const auto segs = split_segments(item);

        if (!records.empty()) {
            if (is_similar(item, segs)) {
                return false;
            }
        }

        const uint32_t new_id = static_cast<uint32_t>(records.size());
        records.push_back(Record{item, segs, new_id});
        ensure_seen_size();

        for (size_t way = 0; way < kWays; ++way) {
            auto &bucket = index[way][segs[way]];
            if (bucket.empty()) {
                occupied_segments_per_way[way]++;
            }
            bucket.push_back(new_id);
        }

        logical_items_any++;
        logical_items_full++;
        return true;
    }

    auto get_name() const -> std::string {
        return "SimHash-4x16-OR";
    }

    auto get_byte_size() const -> size_t {
        size_t bytes = 0;
        bytes += records.capacity() * sizeof(Record);
        for (size_t way = 0; way < kWays; ++way) {
            const auto &way_index = index[way];
            bytes += way_index.size() * sizeof(std::vector<uint32_t>);
            for (const auto &bucket : way_index) {
                bytes += bucket.capacity() * sizeof(uint32_t);
            }
        }
        return bytes;
    }

    auto get_cap() const -> size_t {
        return logical_items_any;
    }

    auto get_add_attempts() const -> size_t {
        return add_attempts;
    }

    auto get_logical_items_any() const -> size_t {
        return logical_items_any;
    }

    auto get_logical_items_full() const -> size_t {
        return logical_items_full;
    }

    auto get_way_occupied_slots() const -> const std::array<size_t, kWays> & {
        return occupied_segments_per_way;
    }

    auto get_total_occupied_slots() const -> size_t {
        size_t total = 0;
        for (auto count : occupied_segments_per_way) {
            total += count;
        }
        return total;
    }

    auto get_total_slot_capacity() const -> size_t {
        return kWays * kSegmentValues;
    }

    auto get_slot_occupancy_ratio() const -> double {
        return static_cast<double>(get_total_occupied_slots()) / static_cast<double>(get_total_slot_capacity());
    }
};

#endif
