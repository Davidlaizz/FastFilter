#ifndef NEWFILTER256_COMPACT_BENCH_HPP
#define NEWFILTER256_COMPACT_BENCH_HPP

#include "index.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace newfilter256_compact_bench {

using ns = std::chrono::nanoseconds;
using u64 = uint64_t;
using Filter = newfilter256_compact::NewFilter256Compact;
using Hash256 = newfilter256_compact::Hash256;

constexpr size_t kDefaultN = 1000000;
constexpr size_t kBenchPrecision = 20;
constexpr size_t kRounds = 9;
constexpr size_t kFinalQueryBatches = 10;
constexpr const char *kFilterName = "NF256C-bin20-fp12-k16";

constexpr const char *kPerfPath = "../scripts/Inputs/NewFilter256Compact";
constexpr const char *kHitRatePath = "../scripts/Inputs/NewFilter256Compact-hitrate";
constexpr const char *kFillPath = "../scripts/Inputs/NewFilter256Compact-fill";
constexpr const char *kConfusionPath = "../scripts/Inputs/NewFilter256Compact-confusion";
constexpr const char *kBuildPath = "../scripts/build-newfilter256compact.csv";
constexpr const char *kFppPath = "../scripts/fpp_table_newfilter256compact.csv";

constexpr const char *kHashFileEnv = "SIMDUP256C_HASH_FILE";
constexpr const char *kShuffleEnv = "SIMDUP256C_SHUFFLE";
constexpr const char *kNEnv = "SIMDUP256C_N";
constexpr const char *kThreadsEnv = "SIMDUP256C_THREADS";

struct PerfRow {
    u64 add_ns = 0;
    u64 uniform_lookup_ns = 0;
    u64 yes_lookup_ns = 0;
    u64 deletions_ns = 0;
};

struct HitRateRow {
    double negative_hit_rate = 0;
    double positive_hit_rate = 0;
    size_t negative_hits = 0;
    size_t positive_hits = 0;
    size_t negative_queries = 0;
    size_t positive_queries = 0;
};

struct FillRow {
    size_t add_attempts = 0;
    size_t logical_items = 0;
    size_t l1_slots_used = 0;
    size_t l2_slots_used = 0;
    size_t total_occupied_slots = 0;
    size_t total_slot_capacity = 0;
    double slot_occupancy_ratio = 0;
    size_t insert_zero_segment = 0;
    size_t insert_after_match1 = 0;
    size_t insert_after_match2 = 0;
    size_t insert_after_match3 = 0;
    size_t insert_after_filter_collision_only = 0;
    size_t dup_match1 = 0;
    size_t dup_match2 = 0;
    size_t dup_match3 = 0;
    size_t dup_match4 = 0;
    size_t collect_calls = 0;
    size_t collect_zero_hits = 0;
    double avg_candidates_per_collect = 0;
    size_t filter_max_hit0 = 0;
    size_t filter_max_hit1 = 0;
    size_t filter_max_hit2 = 0;
    size_t filter_max_hit3 = 0;
    size_t filter_max_hit4 = 0;
    double l1_way_hit_ratio = 0;
    double l2_way_hit_ratio = 0;
    size_t verify_calls = 0;
    size_t verify_candidates_b1 = 0;
    size_t verify_candidates_b2 = 0;
    size_t verify_candidates_b3 = 0;
    size_t verify_candidates_b4 = 0;
    size_t verify_checked_b1 = 0;
    size_t verify_checked_b2 = 0;
    size_t verify_checked_b3 = 0;
    size_t verify_checked_b4 = 0;
    size_t verify_dup_b1 = 0;
    size_t verify_dup_b2 = 0;
    size_t verify_dup_b3 = 0;
    size_t verify_dup_b4 = 0;
    u64 verify_bits_b1 = 0;
    u64 verify_bits_b2 = 0;
    u64 verify_bits_b3 = 0;
    u64 verify_bits_b4 = 0;
    size_t insert_success_total = 0;
    size_t reject_duplicate_total = 0;
    size_t reject_capacity_total = 0;
    size_t reject_total = 0;
};

struct ConfusionRow {
    size_t total_queries = 0;
    size_t positive_queries = 0;
    size_t negative_queries = 0;
    size_t tp = 0;
    size_t fp = 0;
    size_t tn = 0;
    size_t fn = 0;
    double accuracy = 0.0;
    double precision = 0.0;
    double recall = 0.0;
    double f1 = 0.0;
    double tpr = 0.0;
    double fpr = 0.0;
    std::array<size_t, 5> positive_max_match_hist{{0, 0, 0, 0, 0}};
    std::array<size_t, 5> negative_max_match_hist{{0, 0, 0, 0, 0}};
};

inline volatile size_t g_lookup_sink = 0;

inline auto rng() -> std::mt19937_64 & {
    thread_local std::mt19937_64 engine([] {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32u) ^ rd();
    }());
    return engine;
}

inline auto parse_env_size(const char *env_name, size_t default_value) -> size_t {
    const char *value = std::getenv(env_name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }
    const unsigned long long parsed = std::strtoull(value, nullptr, 10);
    if (parsed == 0) {
        return default_value;
    }
    return static_cast<size_t>(parsed);
}

inline auto effective_n() -> size_t {
    return parse_env_size(kNEnv, kDefaultN);
}

inline auto effective_threads() -> size_t {
    return parse_env_size(kThreadsEnv, 1);
}

inline auto random_hash256() -> Hash256 {
    return Hash256{{rng()(), rng()(), rng()(), rng()()}};
}

inline auto flip_random_bits(const Hash256 &src, size_t flip_count) -> Hash256 {
    Hash256 out = src;
    if (flip_count == 0) {
        return out;
    }
    std::array<uint16_t, 256> positions{};
    for (size_t i = 0; i < positions.size(); ++i) {
        positions[i] = static_cast<uint16_t>(i);
    }
    std::shuffle(positions.begin(), positions.end(), rng());
    flip_count = std::min<size_t>(flip_count, positions.size());
    for (size_t i = 0; i < flip_count; ++i) {
        const size_t pos = positions[i];
        const size_t word = pos / 64;
        const size_t bit = pos % 64;
        out.words[word] ^= (1ULL << bit);
    }
    return out;
}

inline void fill_vec_random(std::vector<Hash256> *vec, size_t count) {
    vec->resize(count);
    for (size_t i = 0; i < count; ++i) {
        vec->at(i) = random_hash256();
    }
}

inline bool parse_u64_token(const std::string &token, u64 *value) {
    if (token.empty()) {
        return false;
    }
    int base = 10;
    if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
        base = 16;
    } else {
        for (char ch : token) {
            if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
                base = 16;
                break;
            }
        }
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(token.c_str(), &end, base);
    if (end == token.c_str() || *end != '\0' || errno != 0) {
        return false;
    }
    *value = static_cast<u64>(parsed);
    return true;
}

inline bool parse_hash256_line(const std::string &line, Hash256 *hash) {
    std::string trimmed = line;
    const auto comment_pos = trimmed.find('#');
    if (comment_pos != std::string::npos) {
        trimmed = trimmed.substr(0, comment_pos);
    }
    for (char &ch : trimmed) {
        if (ch == ',' || ch == ';' || ch == '\t') {
            ch = ' ';
        }
    }
    std::istringstream ss(trimmed);
    std::array<u64, 4> words{};
    std::string token;
    size_t count = 0;
    while (ss >> token) {
        u64 value = 0;
        if (!parse_u64_token(token, &value)) {
            continue;
        }
        if (count < 4) {
            words[count++] = value;
        } else {
            break;
        }
    }
    if (count == 4) {
        hash->words = words;
        return true;
    }
    if (count == 1) {
        const u64 seed = words[0];
        hash->words = {seed, seed ^ 0x9E3779B97F4A7C15ULL, seed ^ 0xBF58476D1CE4E5B9ULL, seed ^ 0x94D049BB133111EBULL};
        return true;
    }
    return false;
}

inline bool load_hashes_from_env(std::vector<Hash256> *vec) {
    const char *path = std::getenv(kHashFileEnv);
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open hash file: " << path << std::endl;
        return false;
    }
    vec->clear();
    std::string line;
    while (std::getline(file, line)) {
        Hash256 value{};
        if (parse_hash256_line(line, &value)) {
            vec->push_back(value);
        }
    }
    if (vec->empty()) {
        std::cerr << "Hash file is empty or invalid: " << path << std::endl;
        return false;
    }
    const char *shuffle_flag = std::getenv(kShuffleEnv);
    if (shuffle_flag != nullptr && shuffle_flag[0] != '\0' && shuffle_flag[0] != '0') {
        std::shuffle(vec->begin(), vec->end(), rng());
    }
    return true;
}

inline void ensure_output_dirs() {
    std::filesystem::create_directories("../scripts/Inputs");
}

template<typename Functor>
inline auto time_ns(Functor &&fn) -> u64 {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<ns>(end - start).count();
}

inline auto count_positive(Filter *filter, const std::vector<Hash256> &vec) -> size_t {
    size_t hits = 0;
    for (const auto &value : vec) {
        hits += filter->Find(value);
    }
    g_lookup_sink += hits;
    return hits;
}

inline auto make_positive_sample(const std::vector<Hash256> &base, size_t inserted_prefix_size, size_t sample_size)
    -> std::vector<Hash256> {
    std::vector<Hash256> sample(sample_size);
    if (inserted_prefix_size == 0) {
        return sample;
    }
    std::uniform_int_distribution<size_t> dist(0, inserted_prefix_size - 1);
    for (size_t i = 0; i < sample_size; ++i) {
        sample[i] = base[dist(rng())];
    }
    std::shuffle(sample.begin(), sample.end(), rng());
    return sample;
}

inline auto timed_query_hits_on_range(Filter *filter, const std::vector<Hash256> &vec, size_t start, size_t end)
    -> std::pair<u64, size_t> {
    size_t hits = 0;
    const u64 elapsed = time_ns([&] {
        for (size_t i = start; i < end; ++i) {
            hits += filter->Find(vec[i]);
        }
    });
    g_lookup_sink += hits;
    return {elapsed, hits};
}

inline auto timed_query_hits(Filter *filter, const std::vector<Hash256> &vec) -> std::pair<u64, size_t> {
    size_t hits = 0;
    const u64 elapsed = time_ns([&] {
        for (const auto &value : vec) {
            hits += filter->Find(value);
        }
    });
    g_lookup_sink += hits;
    return {elapsed, hits};
}

inline auto timed_query_eval_on_range(Filter *filter, const std::vector<Hash256> &vec, size_t start, size_t end,
                                      std::array<size_t, 5> *max_match_hist) -> std::pair<u64, size_t> {
    size_t hits = 0;
    const u64 elapsed = time_ns([&] {
        for (size_t i = start; i < end; ++i) {
            const auto eval = filter->Evaluate(vec[i]);
            hits += static_cast<size_t>(eval.is_duplicate);
            const size_t bucket = std::min<size_t>(4, static_cast<size_t>(eval.max_exact_match));
            max_match_hist->at(bucket) += 1;
        }
    });
    g_lookup_sink += hits;
    return {elapsed, hits};
}

inline auto make_positive_queries(const std::vector<Hash256> &base, size_t count) -> std::vector<Hash256> {
    std::vector<Hash256> out(count);
    if (base.empty()) {
        return out;
    }
    std::uniform_int_distribution<size_t> index_dist(0, base.size() - 1);
    std::uniform_int_distribution<int> flip_dist(0, 3);
    for (size_t i = 0; i < count; ++i) {
        const auto &anchor = base[index_dist(rng())];
        out[i] = flip_random_bits(anchor, static_cast<size_t>(flip_dist(rng())));
    }
    return out;
}

inline auto make_negative_queries(const std::vector<Hash256> &base, size_t count) -> std::vector<Hash256> {
    std::vector<Hash256> out(count);
    if (base.empty()) {
        return out;
    }
    std::uniform_int_distribution<size_t> index_dist(0, base.size() - 1);
    std::uniform_int_distribution<int> flip_dist(4, 8);
    for (size_t i = 0; i < count; ++i) {
        const auto &anchor = base[index_dist(rng())];
        out[i] = flip_random_bits(anchor, static_cast<size_t>(flip_dist(rng())));
    }
    return out;
}

inline void append_perf_block(const Filter &filter, u64 init_time_ns, size_t filter_max_capacity,
                              size_t lookup_reps, const std::vector<PerfRow> &rows, const std::string &path) {
    std::fstream file(path, std::fstream::in | std::fstream::out | std::fstream::app);
    file << std::endl;
    file << "# This is a comment." << std::endl;
    file << "NAME\t" << kFilterName << std::endl;
    file << "INIT_TIME(NANO_SECOND)\t" << init_time_ns << std::endl;
    file << "FILTER_MAX_CAPACITY\t" << filter_max_capacity << std::endl;
    file << "BYTE_SIZE\t" << filter.get_byte_size() << std::endl;
    file << "NUMBER_OF_LOOKUP\t" << lookup_reps << std::endl;
    file << std::endl;
    file << "# add, uniform lookup, true_lookup, deletions. Each columns unit is in nano second." << std::endl;
    file << std::endl;
    file << "BENCH_START" << std::endl;
    for (const auto &row : rows) {
        file << row.add_ns << ", " << row.uniform_lookup_ns << ", " << row.yes_lookup_ns << ", " << row.deletions_ns << std::endl;
    }
    file << std::endl;
    file << "BENCH_END" << std::endl;
    file << "END_OF_FILE!" << std::endl;
}

inline void append_hitrate_block(size_t filter_max_capacity, size_t negative_queries_per_round, size_t positive_queries_per_round,
                                 const std::vector<HitRateRow> &rows, const std::string &path) {
    std::fstream file(path, std::fstream::in | std::fstream::out | std::fstream::app);
    file << std::endl;
    file << "# This is a comment." << std::endl;
    file << "NAME\t" << kFilterName << std::endl;
    file << "FILTER_MAX_CAPACITY\t" << filter_max_capacity << std::endl;
    file << "NEGATIVE_QUERIES_PER_ROUND\t" << negative_queries_per_round << std::endl;
    file << "POSITIVE_QUERIES_PER_ROUND\t" << positive_queries_per_round << std::endl;
    file << std::endl;
    file << "HITRATE_START" << std::endl;
    file << std::fixed << std::setprecision(8);
    for (const auto &row : rows) {
        file << row.negative_hit_rate << ", " << row.positive_hit_rate << ", "
             << row.negative_hits << ", " << row.positive_hits << ", "
             << row.negative_queries << ", " << row.positive_queries << std::endl;
    }
    file << std::endl;
    file << "HITRATE_END" << std::endl;
    file << "END_OF_FILE!" << std::endl;
}

inline void append_fill_block(size_t filter_max_capacity, size_t bench_precision, const std::vector<FillRow> &rows,
                              const std::string &path) {
    std::fstream file(path, std::fstream::in | std::fstream::out | std::fstream::app);
    file << std::endl;
    file << "# This is a comment." << std::endl;
    file << "NAME\t" << kFilterName << std::endl;
    file << "FILTER_MAX_CAPACITY\t" << filter_max_capacity << std::endl;
    file << "BENCH_PRECISION\t" << bench_precision << std::endl;
    file << std::endl;
    file << "# add_attempts, logical_items, l1_slots, l2_slots, total_slots, total_capacity, occupancy,"
            " insert_zero, insert_after1, insert_after2, insert_after3, insert_after_filter_collision,"
            " dup1, dup2, dup3, dup4, collect_calls, collect_zero_hits, avg_candidates,"
            " filter_hit0, filter_hit1, filter_hit2, filter_hit3, filter_hit4, l1_way_hit_ratio, l2_way_hit_ratio,"
            " verify_calls, verify_candidates_b1..b4, verify_checked_b1..b4, verify_dup_b1..b4, verify_bits_b1..b4,"
            " insert_success_total, reject_duplicate_total, reject_capacity_total, reject_total." << std::endl;
    file << std::endl;
    file << "FILL_START" << std::endl;
    file << std::fixed << std::setprecision(8);
    for (const auto &row : rows) {
        file << row.add_attempts << ", " << row.logical_items << ", " << row.l1_slots_used << ", " << row.l2_slots_used << ", "
             << row.total_occupied_slots << ", " << row.total_slot_capacity << ", " << row.slot_occupancy_ratio << ", "
             << row.insert_zero_segment << ", " << row.insert_after_match1 << ", " << row.insert_after_match2 << ", "
             << row.insert_after_match3 << ", " << row.insert_after_filter_collision_only << ", "
             << row.dup_match1 << ", " << row.dup_match2 << ", " << row.dup_match3 << ", " << row.dup_match4 << ", "
             << row.collect_calls << ", " << row.collect_zero_hits << ", " << row.avg_candidates_per_collect << ", "
             << row.filter_max_hit0 << ", " << row.filter_max_hit1 << ", " << row.filter_max_hit2 << ", "
             << row.filter_max_hit3 << ", " << row.filter_max_hit4 << ", " << row.l1_way_hit_ratio << ", " << row.l2_way_hit_ratio << ", "
             << row.verify_calls << ", "
             << row.verify_candidates_b1 << ", " << row.verify_candidates_b2 << ", " << row.verify_candidates_b3 << ", " << row.verify_candidates_b4 << ", "
             << row.verify_checked_b1 << ", " << row.verify_checked_b2 << ", " << row.verify_checked_b3 << ", " << row.verify_checked_b4 << ", "
             << row.verify_dup_b1 << ", " << row.verify_dup_b2 << ", " << row.verify_dup_b3 << ", " << row.verify_dup_b4 << ", "
             << row.verify_bits_b1 << ", " << row.verify_bits_b2 << ", " << row.verify_bits_b3 << ", " << row.verify_bits_b4 << ", "
             << row.insert_success_total << ", " << row.reject_duplicate_total << ", "
             << row.reject_capacity_total << ", " << row.reject_total << std::endl;
    }
    file << std::endl;
    file << "FILL_END" << std::endl;
    file << "END_OF_FILE!" << std::endl;
}

inline void append_confusion_block(size_t filter_max_capacity, const ConfusionRow &row, const std::string &path) {
    std::fstream file(path, std::fstream::in | std::fstream::out | std::fstream::app);
    file << std::endl;
    file << "# This is a comment." << std::endl;
    file << "NAME\t" << kFilterName << std::endl;
    file << "FILTER_MAX_CAPACITY\t" << filter_max_capacity << std::endl;
    file << std::endl;
    file << "CONFUSION_START" << std::endl;
    file << "# total, positive, negative, tp, fp, tn, fn, accuracy, precision, recall, f1, tpr, fpr,"
            " pos_match0..4, neg_match0..4" << std::endl;
    file << std::fixed << std::setprecision(8);
    file << row.total_queries << ", " << row.positive_queries << ", " << row.negative_queries << ", "
         << row.tp << ", " << row.fp << ", " << row.tn << ", " << row.fn << ", "
         << row.accuracy << ", " << row.precision << ", " << row.recall << ", " << row.f1 << ", "
         << row.tpr << ", " << row.fpr << ", "
         << row.positive_max_match_hist[0] << ", " << row.positive_max_match_hist[1] << ", "
         << row.positive_max_match_hist[2] << ", " << row.positive_max_match_hist[3] << ", "
         << row.positive_max_match_hist[4] << ", "
         << row.negative_max_match_hist[0] << ", " << row.negative_max_match_hist[1] << ", "
         << row.negative_max_match_hist[2] << ", " << row.negative_max_match_hist[3] << ", "
         << row.negative_max_match_hist[4] << std::endl;
    file << "CONFUSION_END" << std::endl;
    file << "END_OF_FILE!" << std::endl;
}

inline auto capture_fill_row(const Filter &filter) -> FillRow {
    FillRow row{};
    row.add_attempts = filter.get_add_attempts();
    row.logical_items = filter.get_logical_items();
    row.l1_slots_used = filter.get_l1_slots_used();
    row.l2_slots_used = filter.get_l2_slots_used();
    row.total_occupied_slots = filter.get_total_occupied_slots();
    row.total_slot_capacity = filter.get_total_slot_capacity();
    row.slot_occupancy_ratio = filter.get_slot_occupancy_ratio();
    row.insert_zero_segment = filter.get_insert_zero_segment();
    row.insert_after_match1 = filter.get_insert_after_match1();
    row.insert_after_match2 = filter.get_insert_after_match2();
    row.insert_after_match3 = filter.get_insert_after_match3();
    row.insert_after_filter_collision_only = filter.get_insert_after_filter_collision_only();
    row.dup_match1 = filter.get_dup_match1();
    row.dup_match2 = filter.get_dup_match2();
    row.dup_match3 = filter.get_dup_match3();
    row.dup_match4 = filter.get_dup_match4();
    row.collect_calls = filter.get_collect_calls();
    row.collect_zero_hits = filter.get_collect_zero_hit_calls();
    row.avg_candidates_per_collect = (row.collect_calls == 0)
                                         ? 0.0
                                         : static_cast<double>(filter.get_total_candidates_returned()) / static_cast<double>(row.collect_calls);
    row.filter_max_hit0 = filter.get_collect_max_hit_hist(0);
    row.filter_max_hit1 = filter.get_collect_max_hit_hist(1);
    row.filter_max_hit2 = filter.get_collect_max_hit_hist(2);
    row.filter_max_hit3 = filter.get_collect_max_hit_hist(3);
    row.filter_max_hit4 = filter.get_collect_max_hit_hist(4);
    row.l1_way_hit_ratio = (filter.get_l1_way_probe_count() == 0)
                               ? 0.0
                               : static_cast<double>(filter.get_l1_way_hit_count()) /
                                     static_cast<double>(filter.get_l1_way_probe_count());
    row.l2_way_hit_ratio = (filter.get_l2_way_probe_count() == 0)
                               ? 0.0
                               : static_cast<double>(filter.get_l2_way_hit_count()) /
                                     static_cast<double>(filter.get_l2_way_probe_count());
    row.verify_calls = filter.get_verify_calls();
    row.verify_candidates_b1 = filter.get_verify_branch_candidates(1);
    row.verify_candidates_b2 = filter.get_verify_branch_candidates(2);
    row.verify_candidates_b3 = filter.get_verify_branch_candidates(3);
    row.verify_candidates_b4 = filter.get_verify_branch_candidates(4);
    row.verify_checked_b1 = filter.get_verify_branch_checked_candidates(1);
    row.verify_checked_b2 = filter.get_verify_branch_checked_candidates(2);
    row.verify_checked_b3 = filter.get_verify_branch_checked_candidates(3);
    row.verify_checked_b4 = filter.get_verify_branch_checked_candidates(4);
    row.verify_dup_b1 = filter.get_verify_branch_duplicates(1);
    row.verify_dup_b2 = filter.get_verify_branch_duplicates(2);
    row.verify_dup_b3 = filter.get_verify_branch_duplicates(3);
    row.verify_dup_b4 = filter.get_verify_branch_duplicates(4);
    row.verify_bits_b1 = filter.get_verify_branch_compared_bits(1);
    row.verify_bits_b2 = filter.get_verify_branch_compared_bits(2);
    row.verify_bits_b3 = filter.get_verify_branch_compared_bits(3);
    row.verify_bits_b4 = filter.get_verify_branch_compared_bits(4);
    row.insert_success_total = filter.get_insert_success_total();
    row.reject_duplicate_total = filter.get_reject_duplicate_total();
    row.reject_capacity_total = filter.get_reject_capacity_total();
    row.reject_total = row.reject_duplicate_total + row.reject_capacity_total;
    return row;
}

inline void run_perf_single_round(size_t n = effective_n(), size_t bench_precision = kBenchPrecision) {
    ensure_output_dirs();
    std::vector<Hash256> add_vec;
    const bool loaded = load_hashes_from_env(&add_vec);
    if (!loaded) {
        fill_vec_random(&add_vec, n);
    }
    const size_t n_effective = loaded ? add_vec.size() : n;
    const size_t add_step = n_effective / bench_precision;
    if (add_step == 0) {
        std::cerr << "Not enough items for benchmark: n=" << n_effective << std::endl;
        return;
    }
    const size_t final_query_total = add_step;
    const size_t final_positive_queries = (final_query_total * 3) / 10;
    const size_t final_negative_queries = final_query_total - final_positive_queries;

    const auto init_start = std::chrono::high_resolution_clock::now();
    Filter filter(n_effective, effective_threads());
    const auto init_end = std::chrono::high_resolution_clock::now();
    const auto init_time = std::chrono::duration_cast<ns>(init_end - init_start).count();

    std::vector<PerfRow> perf_rows(bench_precision);
    std::vector<HitRateRow> hit_rows(bench_precision);
    std::vector<FillRow> fill_rows(bench_precision);
    ConfusionRow confusion{};

    for (size_t round = 0; round < bench_precision; ++round) {
        const size_t add_start = round * add_step;
        const size_t add_end = add_start + add_step;

        perf_rows[round].add_ns = time_ns([&] {
            for (size_t i = add_start; i < add_end; ++i) {
                filter.Add(add_vec[i]);
            }
        });
        perf_rows[round].uniform_lookup_ns = 0;
        perf_rows[round].yes_lookup_ns = 0;
        perf_rows[round].deletions_ns = 0;
        hit_rows[round].negative_hits = 0;
        hit_rows[round].positive_hits = 0;
        hit_rows[round].negative_queries = 0;
        hit_rows[round].positive_queries = 0;
        hit_rows[round].negative_hit_rate = std::numeric_limits<double>::quiet_NaN();
        hit_rows[round].positive_hit_rate = std::numeric_limits<double>::quiet_NaN();

        fill_rows[round] = capture_fill_row(filter);

        if (round + 1 != bench_precision) {
            continue;
        }

        const auto positive_queries = make_positive_queries(add_vec, final_positive_queries);
        const auto negative_queries = make_negative_queries(add_vec, final_negative_queries);

        u64 total_neg_ns = 0;
        u64 total_pos_ns = 0;
        size_t total_neg_hits = 0;
        size_t total_pos_hits = 0;

        for (size_t batch = 0; batch < kFinalQueryBatches; ++batch) {
            const size_t pos_start = (final_positive_queries * batch) / kFinalQueryBatches;
            const size_t pos_end = (final_positive_queries * (batch + 1)) / kFinalQueryBatches;
            const size_t neg_start = (final_negative_queries * batch) / kFinalQueryBatches;
            const size_t neg_end = (final_negative_queries * (batch + 1)) / kFinalQueryBatches;
            const size_t pos_batch_size = pos_end - pos_start;
            const size_t neg_batch_size = neg_end - neg_start;

            if (neg_batch_size > 0) {
                const auto neg_batch =
                    timed_query_eval_on_range(&filter, negative_queries, neg_start, neg_end, &confusion.negative_max_match_hist);
                total_neg_ns += neg_batch.first;
                total_neg_hits += neg_batch.second;
            }

            if (pos_batch_size > 0) {
                const auto pos_batch =
                    timed_query_eval_on_range(&filter, positive_queries, pos_start, pos_end, &confusion.positive_max_match_hist);
                total_pos_ns += pos_batch.first;
                total_pos_hits += pos_batch.second;
            }
        }

        perf_rows[round].uniform_lookup_ns = total_neg_ns;
        perf_rows[round].yes_lookup_ns = total_pos_ns;

        const size_t tp = total_pos_hits;
        const size_t fn = final_positive_queries - tp;
        const size_t fp = total_neg_hits;
        const size_t tn = final_negative_queries - fp;
        const size_t total = final_positive_queries + final_negative_queries;

        const double accuracy = (total == 0) ? 0.0 : static_cast<double>(tp + tn) / static_cast<double>(total);
        const double precision = ((tp + fp) == 0) ? 0.0 : static_cast<double>(tp) / static_cast<double>(tp + fp);
        const double recall = (final_positive_queries == 0)
                                  ? 0.0
                                  : static_cast<double>(tp) / static_cast<double>(final_positive_queries);
        const double f1 = ((precision + recall) == 0.0) ? 0.0 : (2.0 * precision * recall) / (precision + recall);
        const double tpr = recall;
        const double fpr = (final_negative_queries == 0)
                               ? 0.0
                               : static_cast<double>(fp) / static_cast<double>(final_negative_queries);

        hit_rows[round].negative_hits = fp;
        hit_rows[round].positive_hits = tp;
        hit_rows[round].negative_queries = final_negative_queries;
        hit_rows[round].positive_queries = final_positive_queries;
        hit_rows[round].negative_hit_rate = fpr;
        hit_rows[round].positive_hit_rate = tpr;

        confusion.total_queries = total;
        confusion.positive_queries = final_positive_queries;
        confusion.negative_queries = final_negative_queries;
        confusion.tp = tp;
        confusion.fp = fp;
        confusion.tn = tn;
        confusion.fn = fn;
        confusion.accuracy = accuracy;
        confusion.precision = precision;
        confusion.recall = recall;
        confusion.f1 = f1;
        confusion.tpr = tpr;
        confusion.fpr = fpr;
    }

    append_perf_block(filter, init_time, n_effective, n_effective, perf_rows, kPerfPath);
    append_hitrate_block(n_effective, final_negative_queries, final_positive_queries, hit_rows, kHitRatePath);
    append_fill_block(n_effective, bench_precision, fill_rows, kFillPath);
    append_confusion_block(n_effective, confusion, kConfusionPath);
}

inline auto run_build_single(size_t n = effective_n()) -> u64 {
    std::vector<Hash256> add_vec;
    const bool loaded = load_hashes_from_env(&add_vec);
    if (!loaded) {
        fill_vec_random(&add_vec, n);
    }
    const size_t n_effective = loaded ? add_vec.size() : n;
    Filter filter(n_effective, effective_threads());
    return time_ns([&] {
        for (const auto &item : add_vec) {
            filter.Add(item);
        }
    });
}

inline void run_build_suite(size_t rounds = kRounds, size_t n = effective_n()) {
    std::fstream file(kBuildPath, std::fstream::in | std::fstream::out | std::fstream::app);
    std::vector<Hash256> add_vec;
    const bool loaded = load_hashes_from_env(&add_vec);
    const size_t n_effective = loaded ? add_vec.size() : n;

    file << std::endl;
    file << "n = " << n_effective << std::endl;
    file << kFilterName << ", ";
    for (size_t i = 0; i < rounds; ++i) {
        file << run_build_single(n_effective) << ", ";
    }
    file << std::endl;
}

inline void run_fpp_single(size_t n = effective_n()) {
    std::vector<Hash256> add_vec;
    const bool loaded = load_hashes_from_env(&add_vec);
    if (!loaded) {
        fill_vec_random(&add_vec, n);
    }
    const size_t n_effective = loaded ? add_vec.size() : n;

    std::vector<Hash256> find_vec;
    fill_vec_random(&find_vec, n_effective);

    Filter filter(n_effective, effective_threads());
    for (const auto &item : add_vec) {
        filter.Add(item);
    }

    const size_t yes_on_negatives = count_positive(&filter, find_vec);
    const double positive_ratio = static_cast<double>(yes_on_negatives) / static_cast<double>(find_vec.size());
    const double bpi = static_cast<double>(filter.get_byte_size()) * 8.0 / static_cast<double>(n_effective);
    const double safe_ratio = std::max(positive_ratio, std::numeric_limits<double>::min());
    const double optimal_bits_for_err = -std::log2(safe_ratio);
    const double bpi_diff = bpi - optimal_bits_for_err;
    const double bpi_ratio = bpi / optimal_bits_for_err;

    std::fstream file(kFppPath, std::fstream::in | std::fstream::out | std::fstream::app);
    file << "n =, " << n_effective << ", Lookups =, " << n_effective << std::endl;
    file << "Filter, Size in bytes, Ratio of yes-queries bits per item (average), optimal bits per item (w.r.t. yes-queries), difference of BPI to optimal BPI, ratio of BPI to optimal BPI" << std::endl;
    file << std::setw(34) << std::left << kFilterName << "\t, " << std::setw(12) << std::left << filter.get_byte_size()
         << ", " << std::setw(12) << std::left << positive_ratio
         << ", " << std::setw(12) << std::left << bpi
         << ", " << std::setw(12) << std::left << optimal_bits_for_err
         << ", " << std::setw(12) << std::left << bpi_diff
         << ", " << std::setw(12) << std::left << bpi_ratio
         << std::endl;
}

} // namespace newfilter256_compact_bench

#endif
