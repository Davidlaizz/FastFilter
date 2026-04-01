#ifndef NEW_FILTER_SIMDUP_BENCH_HPP
#define NEW_FILTER_SIMDUP_BENCH_HPP

#include "simhash_4x16_filter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace simdup_bench {

using u64 = uint64_t;
using ns = std::chrono::nanoseconds;

constexpr size_t kN = 65000;
constexpr size_t kBenchPrecision = 20;
constexpr size_t kRounds = 9;
constexpr const char *kPerfPath = "../scripts/Inputs/SimHash-4x16-3of4";
constexpr const char *kHitRatePath = "../scripts/Inputs/SimHash-4x16-3of4-hitrate";
constexpr const char *kFillPath = "../scripts/Inputs/SimHash-4x16-3of4-fill";
constexpr const char *kBuildPath = "../scripts/build-newfilter.csv";
constexpr const char *kFppPath = "../scripts/fpp_table_newfilter.csv";
constexpr const char *kFilterName = "SimHash-4x16-3of4";
constexpr const char *kHashFileEnv = "SIMDUP_HASH_FILE";
constexpr const char *kShuffleEnv = "SIMDUP_SHUFFLE";

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
    size_t logical_items_any = 0;
    size_t logical_items_full = 0;
    size_t total_occupied_slots = 0;
    size_t total_slot_capacity = 0;
    double slot_occupancy_ratio = 0;
};

inline volatile size_t g_lookup_sink = 0;

inline auto rng() -> std::mt19937_64 & {
    thread_local std::mt19937_64 engine([] {
        std::random_device rd;
        const uint64_t seed = (static_cast<uint64_t>(rd()) << 32u) ^ rd();
        return seed;
    }());
    return engine;
}

inline void ensure_output_dirs() {
    std::filesystem::create_directories("../scripts/Inputs");
}

inline void fill_vec_random(std::vector<u64> *vec, size_t number_of_elements) {
    std::uniform_int_distribution<u64> dist(0, std::numeric_limits<u64>::max());
    vec->resize(number_of_elements);
    for (size_t i = 0; i < number_of_elements; ++i) {
        vec->at(i) = dist(rng());
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
        for (char c : token) {
            if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                base = 16;
                break;
            }
        }
    }
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(token.c_str(), &end, base);
    if (end == token.c_str() || *end != '\0' || errno != 0) {
        return false;
    }
    *value = static_cast<u64>(parsed);
    return true;
}

inline bool load_hashes_from_env(std::vector<u64> *vec) {
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
        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        std::istringstream ss(line);
        std::string token;
        if (!(ss >> token)) {
            continue;
        }
        u64 value = 0;
        if (!parse_u64_token(token, &value)) {
            std::string token2;
            if (!(ss >> token2) || !parse_u64_token(token2, &value)) {
                continue;
            }
        }
        vec->push_back(value);
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

template<typename Functor>
inline auto time_ns(Functor &&fn) -> u64 {
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<ns>(t1 - t0).count();
}

inline auto count_positive(const SimHash4x16OrFilter &filter, const std::vector<u64> &vec) -> size_t {
    size_t counter = 0;
    for (const auto item : vec) {
        counter += filter.Find(item);
    }
    g_lookup_sink += counter;
    return counter;
}

inline auto make_positive_sample(const std::vector<u64> &base, size_t inserted_prefix_size, size_t sample_size) -> std::vector<u64> {
    std::vector<u64> sample(sample_size);
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

inline auto timed_query_hits_on_range(const SimHash4x16OrFilter &filter, const std::vector<u64> &vec, size_t start, size_t end) -> std::pair<u64, size_t> {
    size_t hits = 0;
    const u64 elapsed = time_ns([&] {
        for (size_t i = start; i < end; ++i) {
            hits += filter.Find(vec[i]);
        }
    });
    g_lookup_sink += hits;
    return {elapsed, hits};
}

inline auto timed_query_hits(const SimHash4x16OrFilter &filter, const std::vector<u64> &vec) -> std::pair<u64, size_t> {
    size_t hits = 0;
    const u64 elapsed = time_ns([&] {
        for (const auto item : vec) {
            hits += filter.Find(item);
        }
    });
    g_lookup_sink += hits;
    return {elapsed, hits};
}

inline void append_perf_block(const SimHash4x16OrFilter &filter,
                              u64 init_time_ns,
                              size_t filter_max_capacity,
                              size_t lookup_reps,
                              const std::vector<PerfRow> &rows,
                              const std::string &path) {
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

inline void append_hitrate_block(size_t filter_max_capacity,
                                 size_t negative_queries_per_round,
                                 size_t positive_queries_per_round,
                                 const std::vector<HitRateRow> &rows,
                                 const std::string &path) {
    std::fstream file(path, std::fstream::in | std::fstream::out | std::fstream::app);
    file << std::endl;
    file << "# This is a comment." << std::endl;
    file << "NAME\t" << kFilterName << std::endl;
    file << "FILTER_MAX_CAPACITY\t" << filter_max_capacity << std::endl;
    file << "NEGATIVE_QUERIES_PER_ROUND\t" << negative_queries_per_round << std::endl;
    file << "POSITIVE_QUERIES_PER_ROUND\t" << positive_queries_per_round << std::endl;
    file << std::endl;
    file << "# negative_hit_rate, positive_hit_rate, negative_hits, positive_hits, negative_queries, positive_queries." << std::endl;
    file << std::endl;
    file << "HITRATE_START" << std::endl;
    file << std::fixed << std::setprecision(8);
    for (const auto &row : rows) {
        file << row.negative_hit_rate << ", "
             << row.positive_hit_rate << ", "
             << row.negative_hits << ", "
             << row.positive_hits << ", "
             << row.negative_queries << ", "
             << row.positive_queries << std::endl;
    }
    file << std::endl;
    file << "HITRATE_END" << std::endl;
    file << "END_OF_FILE!" << std::endl;
}

inline void append_fill_block(size_t filter_max_capacity,
                              size_t bench_precision,
                              const std::vector<FillRow> &rows,
                              const std::string &path) {
    std::fstream file(path, std::fstream::in | std::fstream::out | std::fstream::app);
    file << std::endl;
    file << "# This is a comment." << std::endl;
    file << "NAME\t" << kFilterName << std::endl;
    file << "FILTER_MAX_CAPACITY\t" << filter_max_capacity << std::endl;
    file << "BENCH_PRECISION\t" << bench_precision << std::endl;
    file << std::endl;
    file << "# add_attempts, logical_items_any, logical_items_full, total_occupied_slots, total_slot_capacity, slot_occupancy_ratio." << std::endl;
    file << std::endl;
    file << "FILL_START" << std::endl;
    file << std::fixed << std::setprecision(8);
    for (const auto &row : rows) {
        file << row.add_attempts << ", "
             << row.logical_items_any << ", "
             << row.logical_items_full << ", "
             << row.total_occupied_slots << ", "
             << row.total_slot_capacity << ", "
             << row.slot_occupancy_ratio << std::endl;
    }
    file << std::endl;
    file << "FILL_END" << std::endl;
    file << "END_OF_FILE!" << std::endl;
}

inline void run_perf_single_round(size_t n = kN, size_t bench_precision = kBenchPrecision) {
    ensure_output_dirs();
    std::vector<u64> add_vec;
    bool loaded = load_hashes_from_env(&add_vec);
    if (!loaded) {
        fill_vec_random(&add_vec, n);
    }
    const size_t n_effective = loaded ? add_vec.size() : n;
    const size_t add_step = n_effective / bench_precision;
    if (add_step == 0) {
        std::cerr << "Not enough items for benchmark: n=" << n_effective << std::endl;
        return;
    }
    const size_t find_step = add_step;
    const size_t true_find_step = add_step;

    const auto init_start = std::chrono::high_resolution_clock::now();
    SimHash4x16OrFilter filter(n_effective);
    const auto init_end = std::chrono::high_resolution_clock::now();
    const auto init_time = std::chrono::duration_cast<ns>(init_end - init_start).count();

    std::vector<PerfRow> rows(bench_precision);
    std::vector<HitRateRow> hit_rows(bench_precision);
    std::vector<FillRow> fill_rows(bench_precision);

    for (size_t round = 0; round < bench_precision; ++round) {
        const size_t add_start = round * add_step;
        const size_t add_end = add_start + add_step;

        rows[round].add_ns = time_ns([&] {
            for (size_t i = add_start; i < add_end; ++i) {
                filter.Add(add_vec[i]);
            }
        });
        const bool has_negative = (round + 1) < bench_precision;
        std::pair<u64, size_t> neg_lookup{0, 0};
        if (has_negative) {
            const size_t neg_start = add_end;
            const size_t neg_end = neg_start + find_step;
            neg_lookup = timed_query_hits_on_range(filter, add_vec, neg_start, neg_end);
            rows[round].uniform_lookup_ns = neg_lookup.first;
        } else {
            rows[round].uniform_lookup_ns = 0;
        }

        const auto positive_sample = make_positive_sample(add_vec, add_end, true_find_step);
        const auto pos_lookup = timed_query_hits(filter, positive_sample);
        rows[round].yes_lookup_ns = pos_lookup.first;
        rows[round].deletions_ns = 0;

        hit_rows[round].negative_hits = neg_lookup.second;
        hit_rows[round].positive_hits = pos_lookup.second;
        hit_rows[round].negative_queries = has_negative ? find_step : 0;
        hit_rows[round].positive_queries = true_find_step;
        hit_rows[round].negative_hit_rate = has_negative
                                                ? static_cast<double>(neg_lookup.second) / static_cast<double>(find_step)
                                                : std::numeric_limits<double>::quiet_NaN();
        hit_rows[round].positive_hit_rate = static_cast<double>(pos_lookup.second) / static_cast<double>(true_find_step);

        fill_rows[round].add_attempts = filter.get_add_attempts();
        fill_rows[round].logical_items_any = filter.get_logical_items_any();
        fill_rows[round].logical_items_full = filter.get_logical_items_full();
        fill_rows[round].total_occupied_slots = filter.get_total_occupied_slots();
        fill_rows[round].total_slot_capacity = filter.get_total_slot_capacity();
        fill_rows[round].slot_occupancy_ratio = filter.get_slot_occupancy_ratio();
    }

    append_perf_block(filter, init_time, n_effective, n_effective, rows, kPerfPath);
    append_hitrate_block(n_effective, find_step, true_find_step, hit_rows, kHitRatePath);
    append_fill_block(n_effective, bench_precision, fill_rows, kFillPath);
}

inline auto run_build_single(size_t n = kN) -> u64 {
    std::vector<u64> add_vec;
    bool loaded = load_hashes_from_env(&add_vec);
    if (!loaded) {
        fill_vec_random(&add_vec, n);
    }
    const size_t n_effective = loaded ? add_vec.size() : n;
    SimHash4x16OrFilter filter(n_effective);
    return time_ns([&] {
        for (const auto item : add_vec) {
            filter.Add(item);
        }
    });
}

inline void run_build_suite(size_t rounds = kRounds, size_t n = kN) {
    std::fstream file(kBuildPath, std::fstream::in | std::fstream::out | std::fstream::app);
    std::vector<u64> add_vec;
    bool loaded = load_hashes_from_env(&add_vec);
    const size_t n_effective = loaded ? add_vec.size() : n;
    file << std::endl;
    file << "n = " << n_effective << std::endl;
    file << kFilterName << ", ";
    for (size_t i = 0; i < rounds; ++i) {
        file << run_build_single(n_effective) << ", ";
    }
    file << std::endl;
}

inline void run_fpp_single(size_t n = kN) {
    std::vector<u64> add_vec, find_vec;
    bool loaded = load_hashes_from_env(&add_vec);
    if (!loaded) {
        fill_vec_random(&add_vec, n);
    }
    const size_t n_effective = loaded ? add_vec.size() : n;
    fill_vec_random(&find_vec, n_effective);

    SimHash4x16OrFilter filter(n_effective);
    for (const auto item : add_vec) {
        filter.Add(item);
    }

    const size_t yes_on_negatives = count_positive(filter, find_vec);
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

}// namespace simdup_bench

#endif
