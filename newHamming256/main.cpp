#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
struct Hash256 {
    uint64_t words[4];
};

struct Config {
    size_t n_start = 50000;
    size_t n_end = 1000000;
    size_t n_step = 50000;
    size_t query_count = 10000;
    uint64_t seed = 42;
    std::string output_csv = "../scripts/hamming256_linear.csv";
};

auto parse_size_arg(const char *value) -> size_t {
    return static_cast<size_t>(std::stoull(value));
}

auto parse_u64_arg(const char *value) -> uint64_t {
    return std::stoull(value);
}

auto parse_args(int argc, char **argv, Config *config) -> bool {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--n-start" && i + 1 < argc) {
            config->n_start = parse_size_arg(argv[++i]);
        } else if (arg == "--n-end" && i + 1 < argc) {
            config->n_end = parse_size_arg(argv[++i]);
        } else if (arg == "--n-step" && i + 1 < argc) {
            config->n_step = parse_size_arg(argv[++i]);
        } else if (arg == "--queries" && i + 1 < argc) {
            config->query_count = parse_size_arg(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            config->seed = parse_u64_arg(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            config->output_csv = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: measure_hamming256_linear [options]\n"
                << "  --n-start <int>   default 50000\n"
                << "  --n-end <int>     default 1000000\n"
                << "  --n-step <int>    default 50000\n"
                << "  --queries <int>   default 10000\n"
                << "  --seed <int>      default 42\n"
                << "  --out <path>      default ../scripts/hamming256_linear.csv\n";
            return false;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            return false;
        }
    }
    return true;
}

inline auto hamming_distance_256(const Hash256 &left, const Hash256 &right) -> uint32_t {
    return static_cast<uint32_t>(
        __builtin_popcountll(left.words[0] ^ right.words[0]) +
        __builtin_popcountll(left.words[1] ^ right.words[1]) +
        __builtin_popcountll(left.words[2] ^ right.words[2]) +
        __builtin_popcountll(left.words[3] ^ right.words[3]));
}

auto generate_hashes(size_t count, std::mt19937_64 *rng) -> std::vector<Hash256> {
    std::vector<Hash256> values(count);
    for (size_t i = 0; i < count; ++i) {
        values[i].words[0] = (*rng)();
        values[i].words[1] = (*rng)();
        values[i].words[2] = (*rng)();
        values[i].words[3] = (*rng)();
    }
    return values;
}
} // namespace

int main(int argc, char **argv) {
    Config config;
    if (!parse_args(argc, argv, &config)) {
        return (argc > 1) ? 1 : 0;
    }

    if (config.n_start == 0 || config.n_step == 0 || config.n_start > config.n_end || config.query_count == 0) {
        std::cerr << "Invalid parameters.\n";
        return 2;
    }

    std::mt19937_64 rng(config.seed);
    const size_t max_n = config.n_end;
    auto database = generate_hashes(max_n, &rng);
    auto queries = generate_hashes(config.query_count, &rng);

    const std::filesystem::path out_path(config.output_csv);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path());
    }

    std::ofstream out(config.output_csv);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << config.output_csv << '\n';
        return 3;
    }

    out << "n,queries,comparisons,total_time_sec,comparisons_per_sec,checksum\n";
    out << std::fixed << std::setprecision(9);

    uint64_t global_checksum = 0;
    for (size_t n = config.n_start; n <= config.n_end; n += config.n_step) {
        uint64_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();

        for (size_t qi = 0; qi < config.query_count; ++qi) {
            const auto &query = queries[qi];
            for (size_t di = 0; di < n; ++di) {
                checksum += hamming_distance_256(query, database[di]);
            }
        }

        const auto end = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = end - start;
        const double total_time_sec = elapsed.count();
        const double comparisons = static_cast<double>(n) * static_cast<double>(config.query_count);
        const double comparisons_per_sec = (total_time_sec > 0.0) ? (comparisons / total_time_sec) : 0.0;

        global_checksum ^= checksum;
        out << n << ',' << config.query_count << ',' << std::setprecision(0) << comparisons << ','
            << std::setprecision(9) << total_time_sec << ',' << comparisons_per_sec << ',' << checksum << '\n';

        std::cout << "[n=" << n << "] time=" << std::setprecision(6) << total_time_sec
                  << " sec, throughput=" << std::setprecision(3) << comparisons_per_sec
                  << " comps/sec, checksum=" << checksum << '\n';

        if (n > config.n_end - config.n_step) {
            break;
        }
    }

    std::cout << "Done. CSV: " << config.output_csv << '\n';
    std::cout << "Global checksum: " << global_checksum << '\n';
    return 0;
}
