#include "simdup_bench.hpp"

int main() {
    for (size_t i = 0; i < simdup_bench::kRounds; ++i) {
        simdup_bench::run_perf_single_round();
    }
    return 0;
}
