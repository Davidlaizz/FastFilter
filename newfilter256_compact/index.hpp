#ifndef NEWFILTER256_COMPACT_INDEX_HPP
#define NEWFILTER256_COMPACT_INDEX_HPP

#include "compact_filter_layer.hpp"
#include "compact_verify_layer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace newfilter256_compact {

class NewFilter256Compact {
private:
    CompactFilterLayer filter_;
    CompactVerifyLayer verifier_;

    size_t add_attempts_ = 0;
    size_t logical_items_ = 0;

    size_t insert_zero_segment_ = 0;
    size_t insert_after_match1_ = 0;
    size_t insert_after_match2_ = 0;
    size_t insert_after_match3_ = 0;
    size_t insert_after_filter_collision_only_ = 0;

    size_t dup_match1_ = 0;
    size_t dup_match2_ = 0;
    size_t dup_match3_ = 0;
    size_t dup_match4_ = 0;

public:
    explicit NewFilter256Compact(size_t expected_items = 0, size_t verify_threads = 1)
        : filter_(expected_items), verifier_(expected_items, verify_threads) {
    }

    inline void set_verify_threads(size_t value) {
        verifier_.set_thread_count(value);
    }

    inline auto Add(const Hash256 &item) -> bool {
        ++add_attempts_;
        const auto segments = split_segments(item);
        const auto metas = CompactFilterLayer::build_metas(item);

        std::vector<CandidateEntry> candidates;
        FilterCollectSummary collect_summary{};
        filter_.collect_candidates(metas, verifier_.size(), &candidates, &collect_summary);

        VerifyDecision decision{};
        bool is_dup = false;
        if (!candidates.empty()) {
            is_dup = verifier_.verify_duplicate(item, candidates, &decision);
        }
        if (is_dup) {
            switch (decision.duplicate_exact_match) {
                case 1: ++dup_match1_; break;
                case 2: ++dup_match2_; break;
                case 3: ++dup_match3_; break;
                case 4: ++dup_match4_; break;
                default: break;
            }
            return false;
        }

        if (decision.max_exact_match == 0) {
            if (candidates.empty()) {
                ++insert_zero_segment_;
            } else {
                ++insert_after_filter_collision_only_;
            }
        } else if (decision.max_exact_match == 1) {
            ++insert_after_match1_;
        } else if (decision.max_exact_match == 2) {
            ++insert_after_match2_;
        } else if (decision.max_exact_match >= 3) {
            ++insert_after_match3_;
        }

        const uint32_t id = verifier_.append_record(item, segments);
        filter_.insert_record(id, metas);
        ++logical_items_;
        return true;
    }

    inline auto Find(const Hash256 &item) -> bool {
        const auto metas = CompactFilterLayer::build_metas(item);
        std::vector<CandidateEntry> candidates;
        FilterCollectSummary collect_summary{};
        filter_.collect_candidates(metas, verifier_.size(), &candidates, &collect_summary);

        if (candidates.empty()) {
            return false;
        }
        VerifyDecision decision{};
        return verifier_.verify_duplicate(item, candidates, &decision);
    }

    auto get_name() const -> std::string {
        return "NF256C-bin20-fp12-k16";
    }

    auto get_add_attempts() const -> size_t {
        return add_attempts_;
    }

    auto get_logical_items() const -> size_t {
        return logical_items_;
    }

    auto get_l1_slots_used() const -> size_t {
        return filter_.get_l1_slots_used();
    }

    auto get_l2_slots_used() const -> size_t {
        return filter_.get_l2_slots_used();
    }

    auto get_total_occupied_slots() const -> size_t {
        return filter_.get_total_occupied_slots();
    }

    auto get_total_slot_capacity() const -> size_t {
        return filter_.get_total_slot_capacity();
    }

    auto get_slot_occupancy_ratio() const -> double {
        return filter_.get_slot_occupancy_ratio();
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

    auto get_insert_after_filter_collision_only() const -> size_t {
        return insert_after_filter_collision_only_;
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

    auto get_collect_calls() const -> size_t {
        return filter_.get_collect_calls();
    }

    auto get_collect_zero_hit_calls() const -> size_t {
        return filter_.get_collect_zero_hit_calls();
    }

    auto get_total_candidates_returned() const -> size_t {
        return filter_.get_total_candidates_returned();
    }

    auto get_collect_max_hit_hist(size_t hit_count) const -> size_t {
        return filter_.get_collect_max_hit_hist(hit_count);
    }

    auto get_l1_way_probe_count() const -> size_t {
        return filter_.get_l1_way_probe_count();
    }

    auto get_l1_way_hit_count() const -> size_t {
        return filter_.get_l1_way_hit_count();
    }

    auto get_l2_way_probe_count() const -> size_t {
        return filter_.get_l2_way_probe_count();
    }

    auto get_l2_way_hit_count() const -> size_t {
        return filter_.get_l2_way_hit_count();
    }

    auto get_verify_calls() const -> size_t {
        return verifier_.get_verify_calls();
    }

    auto get_verify_branch_candidates(size_t exact_count) const -> size_t {
        return verifier_.get_verify_branch_candidates(exact_count);
    }

    auto get_verify_branch_checked_candidates(size_t exact_count) const -> size_t {
        return verifier_.get_verify_branch_checked_candidates(exact_count);
    }

    auto get_verify_branch_duplicates(size_t exact_count) const -> size_t {
        return verifier_.get_verify_branch_duplicates(exact_count);
    }

    auto get_verify_branch_compared_bits(size_t exact_count) const -> uint64_t {
        return verifier_.get_verify_branch_compared_bits(exact_count);
    }

    auto get_byte_size() const -> size_t {
        return filter_.get_byte_size() + verifier_.get_byte_size();
    }
};

} // namespace newfilter256_compact

#endif
