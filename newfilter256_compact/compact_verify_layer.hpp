#ifndef NEWFILTER256_COMPACT_VERIFY_LAYER_HPP
#define NEWFILTER256_COMPACT_VERIFY_LAYER_HPP

#include "common.hpp"
#include "compact_filter_layer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace newfilter256_compact {

struct VerifyDecision {
    bool is_duplicate = false;
    uint8_t duplicate_exact_match = 0;
    uint8_t max_exact_match = 0;
    size_t candidate_count = 0;
    size_t checked_candidates = 0;
    uint64_t checked_bits = 0;
};

class CompactVerifyLayer {
public:
    static constexpr int kMaxHammingDistance = 3;

private:
    struct Record {
        Hash256 full{};
        std::array<u64, 4> segments{};
        uint32_t id = 0;
    };

    struct BranchCandidate {
        uint32_t id = 0;
        uint8_t exact_count = 0;
        uint8_t exact_mask = 0;
    };

    std::vector<Record> records_{};
    size_t thread_count_ = 1;

    size_t verify_calls_ = 0;
    std::array<size_t, 5> verify_branch_candidates_{{0, 0, 0, 0, 0}};
    std::array<size_t, 5> verify_branch_duplicates_{{0, 0, 0, 0, 0}};
    std::array<uint64_t, 5> verify_branch_compared_bits_{{0, 0, 0, 0, 0}};
    std::array<size_t, 5> verify_branch_checked_candidates_{{0, 0, 0, 0, 0}};

    inline static auto exact_match_mask(const std::array<u64, 4> &lhs, const std::array<u64, 4> &rhs) -> uint8_t {
        uint8_t mask = 0;
        for (size_t i = 0; i < 4; ++i) {
            if (lhs[i] == rhs[i]) {
                mask = static_cast<uint8_t>(mask | static_cast<uint8_t>(1u << i));
            }
        }
        return mask;
    }

    inline static auto popcount4(uint8_t value) -> uint8_t {
        return static_cast<uint8_t>(popcount64(static_cast<u64>(value)));
    }

    inline static auto remaining_hamming(const std::array<u64, 4> &query_segments,
                                         const std::array<u64, 4> &candidate_segments,
                                         uint8_t exact_mask) -> int {
        int distance = 0;
        for (size_t i = 0; i < 4; ++i) {
            if ((exact_mask & (1u << i)) != 0) {
                continue;
            }
            distance += static_cast<int>(popcount64(query_segments[i] ^ candidate_segments[i]));
            if (distance > kMaxHammingDistance) {
                return distance;
            }
        }
        return distance;
    }

    auto check_branch_sequential(const std::vector<BranchCandidate> &branch_items, uint8_t exact_count,
                                 const std::array<u64, 4> &query_segments, VerifyDecision *decision) -> bool {
        const uint64_t bits_per_compare = static_cast<uint64_t>((4 - exact_count) * 64);
        verify_branch_candidates_[exact_count] += branch_items.size();
        for (const auto &item : branch_items) {
            ++verify_branch_checked_candidates_[exact_count];
            ++decision->checked_candidates;
            decision->checked_bits += bits_per_compare;
            verify_branch_compared_bits_[exact_count] += bits_per_compare;
            if (exact_count == 4) {
                decision->is_duplicate = true;
                decision->duplicate_exact_match = 4;
                ++verify_branch_duplicates_[4];
                return true;
            }

            const auto &record = records_[item.id];
            if (remaining_hamming(query_segments, record.segments, item.exact_mask) <= kMaxHammingDistance) {
                decision->is_duplicate = true;
                decision->duplicate_exact_match = exact_count;
                ++verify_branch_duplicates_[exact_count];
                return true;
            }
        }
        return false;
    }

    auto check_branch_parallel(const std::vector<BranchCandidate> &branch_items, uint8_t exact_count,
                               const std::array<u64, 4> &query_segments, VerifyDecision *decision) -> bool {
        const uint64_t bits_per_compare = static_cast<uint64_t>((4 - exact_count) * 64);
        verify_branch_candidates_[exact_count] += branch_items.size();
        if (exact_count == 4) {
            ++verify_branch_checked_candidates_[4];
            decision->checked_candidates += 1;
            ++verify_branch_duplicates_[4];
            decision->is_duplicate = true;
            decision->duplicate_exact_match = 4;
            return true;
        }

        const size_t workers = std::min(thread_count_, branch_items.size());
        const size_t chunk = (branch_items.size() + workers - 1) / workers;
        std::atomic<bool> found{false};
        std::vector<std::thread> threads;
        threads.reserve(workers);

        struct LocalStat {
            size_t checked = 0;
            uint64_t bits = 0;
        };
        std::vector<LocalStat> local_stats(workers);

        for (size_t t = 0; t < workers; ++t) {
            const size_t begin = t * chunk;
            const size_t end = std::min(branch_items.size(), begin + chunk);
            threads.emplace_back([&, begin, end, t] {
                for (size_t idx = begin; idx < end; ++idx) {
                    if (found.load(std::memory_order_relaxed)) {
                        break;
                    }
                    ++local_stats[t].checked;
                    local_stats[t].bits += bits_per_compare;
                    const auto &item = branch_items[idx];
                    const auto &record = records_[item.id];
                    if (remaining_hamming(query_segments, record.segments, item.exact_mask) <= kMaxHammingDistance) {
                        found.store(true, std::memory_order_relaxed);
                        break;
                    }
                }
            });
        }
        for (auto &th : threads) {
            th.join();
        }

        size_t checked_total = 0;
        uint64_t bits_total = 0;
        for (const auto &stat : local_stats) {
            checked_total += stat.checked;
            bits_total += stat.bits;
        }
        verify_branch_checked_candidates_[exact_count] += checked_total;
        verify_branch_compared_bits_[exact_count] += bits_total;
        decision->checked_candidates += checked_total;
        decision->checked_bits += bits_total;

        if (!found.load(std::memory_order_relaxed)) {
            return false;
        }
        decision->is_duplicate = true;
        decision->duplicate_exact_match = exact_count;
        ++verify_branch_duplicates_[exact_count];
        return true;
    }

public:
    explicit CompactVerifyLayer(size_t expected_items = 0, size_t thread_count = 1)
        : thread_count_(std::max<size_t>(1, thread_count)) {
        records_.reserve(expected_items);
    }

    inline void set_thread_count(size_t value) {
        thread_count_ = std::max<size_t>(1, value);
    }

    inline auto size() const -> size_t {
        return records_.size();
    }

    inline auto append_record(const Hash256 &item, const std::array<u64, 4> &segments) -> uint32_t {
        const uint32_t id = static_cast<uint32_t>(records_.size());
        records_.push_back(Record{item, segments, id});
        return id;
    }

    inline auto verify_duplicate(const Hash256 &item,
                                 const std::vector<CandidateEntry> &candidates,
                                 VerifyDecision *decision) -> bool {
        ++verify_calls_;
        *decision = VerifyDecision{};
        decision->candidate_count = candidates.size();
        if (candidates.empty()) {
            return false;
        }

        const auto query_segments = split_segments(item);
        std::array<std::vector<BranchCandidate>, 5> branches{};
        for (const auto &candidate : candidates) {
            if (candidate.id >= records_.size()) {
                continue;
            }
            const auto &record = records_[candidate.id];
            const uint8_t mask = exact_match_mask(query_segments, record.segments);
            const uint8_t exact_count = popcount4(mask);
            if (exact_count == 0) {
                continue;
            }
            decision->max_exact_match = std::max<uint8_t>(decision->max_exact_match, exact_count);
            branches[exact_count].push_back(BranchCandidate{candidate.id, exact_count, mask});
        }

        for (int exact = 4; exact >= 1; --exact) {
            const auto &branch = branches[exact];
            if (branch.empty()) {
                continue;
            }
            if (thread_count_ > 1 && branch.size() >= 2048 && exact <= 2) {
                if (check_branch_parallel(branch, static_cast<uint8_t>(exact), query_segments, decision)) {
                    return true;
                }
                continue;
            }
            if (check_branch_sequential(branch, static_cast<uint8_t>(exact), query_segments, decision)) {
                return true;
            }
        }
        return false;
    }

    auto get_verify_calls() const -> size_t {
        return verify_calls_;
    }

    auto get_verify_branch_candidates(size_t exact_count) const -> size_t {
        if (exact_count >= verify_branch_candidates_.size()) {
            return 0;
        }
        return verify_branch_candidates_[exact_count];
    }

    auto get_verify_branch_duplicates(size_t exact_count) const -> size_t {
        if (exact_count >= verify_branch_duplicates_.size()) {
            return 0;
        }
        return verify_branch_duplicates_[exact_count];
    }

    auto get_verify_branch_checked_candidates(size_t exact_count) const -> size_t {
        if (exact_count >= verify_branch_checked_candidates_.size()) {
            return 0;
        }
        return verify_branch_checked_candidates_[exact_count];
    }

    auto get_verify_branch_compared_bits(size_t exact_count) const -> uint64_t {
        if (exact_count >= verify_branch_compared_bits_.size()) {
            return 0;
        }
        return verify_branch_compared_bits_[exact_count];
    }

    auto get_byte_size() const -> size_t {
        return records_.capacity() * sizeof(Record);
    }
};

} // namespace newfilter256_compact

#endif
