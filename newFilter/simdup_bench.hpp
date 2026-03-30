#ifndef NEW_FILTER_SIMDUP_BENCH_HPP
#define NEW_FILTER_SIMDUP_BENCH_HPP

#include "simhash_4x16_filter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace simdup_bench {

using u64 = uint64_t;
using ns = std::chrono::nanoseconds;

constexpr size_t kN = 100000;
constexpr size_t kBenchPrecision = 20;
constexpr size_t kRounds = 9;
constexpr const char *kPerfPath = "../scripts/Inputs/SimHash-4x16-OR";
constexpr const char *kBuildPath = "../scripts/build-newfilter.csv";
constexpr const char *kFppPath = "../scripts/fpp_table_newfilter.csv";
constexpr const char *kFilterName = "SimHash-4x16-OR";

struct PerfRow {
    u64 add_ns = 0;
    u64 uniform_lookup_ns = 0;
    u64 yes_lookup_ns = 0;
    u64 deletions_ns = 0;
};

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

inline void run_perf_single_round(size_t n = kN, size_t bench_precision = kBenchPrecision) {
    ensure_output_dirs();
    const size_t add_step = n / bench_precision;
    const size_t find_step = n / bench_precision;
    const size_t true_find_step = add_step;

    std::vector<u64> add_vec, find_vec;
    fill_vec_random(&add_vec, n);
    fill_vec_random(&find_vec, n);

    const auto init_start = std::chrono::high_resolution_clock::now();
    SimHash4x16OrFilter filter(n);
    const auto init_end = std::chrono::high_resolution_clock::now();
    const auto init_time = std::chrono::duration_cast<ns>(init_end - init_start).count();

    std::vector<PerfRow> rows(bench_precision);
    for (size_t round = 0; round < bench_precision; ++round) {
        const size_t add_start = round * add_step;
        const size_t add_end = add_start + add_step;
        const size_t find_start = round * find_step;
        const size_t find_end = find_start + find_step;

        rows[round].add_ns = time_ns([&] {
            for (size_t i = add_start; i < add_end; ++i) {
                filter.Add(add_vec[i]);
            }
        });

        rows[round].uniform_lookup_ns = time_ns([&] {
            for (size_t i = find_start; i < find_end; ++i) {
                filter.Find(find_vec[i]);
            }
        });

        const auto positive_sample = make_positive_sample(add_vec, add_end, true_find_step);
        rows[round].yes_lookup_ns = time_ns([&] {
            for (const auto item : positive_sample) {
                filter.Find(item);
            }
        });
        rows[round].deletions_ns = 0;
    }

    append_perf_block(filter, init_time, n, n, rows, kPerfPath);
}

inline auto run_build_single(size_t n = kN) -> u64 {
    std::vector<u64> add_vec;
    fill_vec_random(&add_vec, n);
    SimHash4x16OrFilter filter(n);
    return time_ns([&] {
        for (const auto item : add_vec) {
            filter.Add(item);
        }
    });
}

inline void run_build_suite(size_t rounds = kRounds, size_t n = kN) {
    std::fstream file(kBuildPath, std::fstream::in | std::fstream::out | std::fstream::app);
    file << std::endl;
    file << "n = " << n << std::endl;
    file << kFilterName << ", ";
    for (size_t i = 0; i < rounds; ++i) {
        file << run_build_single(n) << ", ";
    }
    file << std::endl;
}

inline void run_fpp_single(size_t n = kN) {
    std::vector<u64> add_vec, find_vec;
    fill_vec_random(&add_vec, n);
    fill_vec_random(&find_vec, n);

    SimHash4x16OrFilter filter(n);
    for (const auto item : add_vec) {
        filter.Add(item);
    }

    const size_t yes_on_negatives = count_positive(filter, find_vec);
    const double positive_ratio = static_cast<double>(yes_on_negatives) / static_cast<double>(find_vec.size());
    const double bpi = static_cast<double>(filter.get_byte_size()) * 8.0 / static_cast<double>(n);
    const double safe_ratio = std::max(positive_ratio, std::numeric_limits<double>::min());
    const double optimal_bits_for_err = -std::log2(safe_ratio);
    const double bpi_diff = bpi - optimal_bits_for_err;
    const double bpi_ratio = bpi / optimal_bits_for_err;

    std::fstream file(kFppPath, std::fstream::in | std::fstream::out | std::fstream::app);
    file << "n =, " << n << ", Lookups =, " << n << std::endl;
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
