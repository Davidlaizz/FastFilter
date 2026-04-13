#include "bench.hpp"

int main() {
    for (size_t i = 0; i < newfilter256_bench::kRounds; ++i) {
        newfilter256_bench::run_perf_single_round();
    }
    return 0;
}
