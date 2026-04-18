cmake_minimum_required(VERSION 3.10)

# set(CMAKE_C_COMPILER gcc-10)
# set(CMAKE_CXX_COMPILER g++-10)
# set(CMAKE_C_COMPILER clang)
# set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 17)
project(APD_ONLY)
find_package(Threads REQUIRED)

# set(COMPILE_FLAGS "-fprofile-instr-generate -fcoverage-mapping")
# set(CMAKE_EXE_LINKER_FLAGS "-fprofile-instr-generate")

set(CMAKE_CXX_FLAGS "-march=native")

# Seems like those are necessary.
# set(CMAKE_CXX_FLAGS "-mavx2 -mbmi -mbmi2 -mlzcnt -mpopcnt -mavx512vl -mavx512vbmi -mavx512dq -mavx512f -mavx512cd")

#-fsanitize=undefined
#-fsanitize=memory
#set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined -fno-omit-frame-pointer")
#set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")

set(PF_FILES
        Prefix-Filter/Shift_op.hpp Prefix-Filter/Shift_op.cpp
        Prefix-Filter/min_pd256.hpp Prefix-Filter/min_pd256.cpp
)

set(CF_FILES
        cuckoofilter/src/bitsutil.h cuckoofilter/src/permencoding.h
        cuckoofilter/src/printutil.cc cuckoofilter/src/debug.h
        cuckoofilter/src/singletable.h cuckoofilter/src/packedtable.h
        cuckoofilter/src/cuckoofilter.h
        cuckoofilter/src/cuckoofilter_stable.h
        )

set(TESTS_FILES 
        hashutil.h
        Tests/wrappers.hpp
        Tests/smart_tests.hpp
        Tests/smart_tests.cpp
        # Tests/PerfEvent.hpp
        )

set(TC_SHORTCUT_FILES
#        hashutil.h
        TC-Shortcut/tc-sym.hpp
        TC-Shortcut/tc-sym.cpp
        TC-Shortcut/TC-shortcut.hpp)


set(Bloom_Files
        Bloom_Filter/simd-block.h
        Bloom_Filter/simd-block-fixed-fpp.h
        Bloom_Filter/Impala512.h
        Bloom_Filter/bloom.hpp
        # Bloom_Filter/bloom-simple.hpp
)

set(ALL_FILES_NM
        ${Bloom_Files}
        ${TESTS_FILES}
        ${PF_FILES}
        
        ${CF_FILES}
        ${TC_SHORTCUT_FILES}
        
        )
set(ALL_FILES
        ${ALL_FILES_NM}
        # ${XOR_FILES}
        all_main.cpp
        )

set(NEWFILTER_FILES
        newFilter/simhash_4x16_filter.hpp
        newFilter/simdup_bench.hpp
)

set(NEWFILTER256_FILES
        newfilter256/prefix_filter_256.hpp
        newfilter256/bench.hpp
)

set(NEWFILTER256_COMPACT_FILES
        newfilter256_compact/common.hpp
        newfilter256_compact/compact_filter_layer.hpp
        newfilter256_compact/compact_verify_layer.hpp
        newfilter256_compact/index.hpp
        newfilter256_compact/bench.hpp
)


################################################################################
################################################################################
################################################################################
################################################################################
# Measure built
add_executable(measure_built0 ${ALL_FILES_NM} main-built.cpp)
add_executable(measure_built3 ${ALL_FILES_NM} main-built.cpp)
add_executable(measure_built ${ALL_FILES_NM} main-built.cpp)
target_compile_options(measure_built0 PRIVATE -O0 -g3 -W)
target_compile_options(measure_built3 PRIVATE -O3 -g3)
target_compile_options(measure_built PRIVATE -Ofast -DNDEBUG)

################################################################################

# Measure fpp
add_executable(measure_fpp0 ${ALL_FILES_NM} main-fpp.cpp)
add_executable(measure_fpp3 ${ALL_FILES_NM} main-fpp.cpp)
add_executable(measure_fpp ${ALL_FILES_NM} main-fpp.cpp)
target_compile_options(measure_fpp0 PRIVATE -O0 -g3 -W)
target_compile_options(measure_fpp3 PRIVATE -O3 -g3)
target_compile_options(measure_fpp PRIVATE -Ofast -DNDEBUG )

################################################################################

# Measure perf
add_executable(measure_perf0 ${ALL_FILES_NM} main-perf.cpp)
add_executable(measure_perf3 ${ALL_FILES_NM} main-perf.cpp)
add_executable(measure_perf ${ALL_FILES_NM} main-perf.cpp)
target_compile_options(measure_perf0 PRIVATE -O0 -g3 -W)
target_compile_options(measure_perf3 PRIVATE -O3 -g3)
target_compile_options(measure_perf PRIVATE -Ofast -DNDEBUG)

################################################################################

# Measure example
add_executable(example0 ${ALL_FILES_NM} example.cpp)
add_executable(example ${ALL_FILES_NM} example.cpp)
target_compile_options(example0 PRIVATE -O0 -g3 -W)
target_compile_options(example PRIVATE -Ofast -DNDEBUG)

################################################################################

# Measure newFilter only (n=65000)
add_executable(measure_newfilter_perf ${NEWFILTER_FILES} newFilter/main-perf.cpp)
target_compile_options(measure_newfilter_perf PRIVATE -Ofast -DNDEBUG)

add_executable(measure_newfilter_built ${NEWFILTER_FILES} newFilter/main-built.cpp)
target_compile_options(measure_newfilter_built PRIVATE -Ofast -DNDEBUG)

add_executable(measure_newfilter_fpp ${NEWFILTER_FILES} newFilter/main-fpp.cpp)
target_compile_options(measure_newfilter_fpp PRIVATE -Ofast -DNDEBUG)


################################################################################

# Measure newfilter256 only (256-bit split 4x64)
add_executable(measure_newfilter256_perf ${NEWFILTER256_FILES} newfilter256/main-perf.cpp)
add_executable(measure_newfilter256_built ${NEWFILTER256_FILES} newfilter256/main-built.cpp)
add_executable(measure_newfilter256_fpp ${NEWFILTER256_FILES} newfilter256/main-fpp.cpp)

target_compile_options(measure_newfilter256_perf PRIVATE -Ofast -DNDEBUG)
target_compile_options(measure_newfilter256_built PRIVATE -Ofast -DNDEBUG)
target_compile_options(measure_newfilter256_fpp PRIVATE -Ofast -DNDEBUG)

################################################################################

# Measure newfilter256_compact (256-bit split 4x64, compact L1/L2 + verify layer)
add_executable(measure_newfilter256_compact_perf ${NEWFILTER256_COMPACT_FILES} newfilter256_compact/main-perf.cpp)
add_executable(measure_newfilter256_compact_built ${NEWFILTER256_COMPACT_FILES} newfilter256_compact/main-built.cpp)
add_executable(measure_newfilter256_compact_fpp ${NEWFILTER256_COMPACT_FILES} newfilter256_compact/main-fpp.cpp)

target_compile_options(measure_newfilter256_compact_perf PRIVATE -Ofast -DNDEBUG)
target_compile_options(measure_newfilter256_compact_built PRIVATE -Ofast -DNDEBUG)
target_compile_options(measure_newfilter256_compact_fpp PRIVATE -Ofast -DNDEBUG)
target_link_libraries(measure_newfilter256_compact_perf PRIVATE Threads::Threads)
target_link_libraries(measure_newfilter256_compact_built PRIVATE Threads::Threads)
target_link_libraries(measure_newfilter256_compact_fpp PRIVATE Threads::Threads)

################################################################################

# 256-bit hamming linearity benchmark
add_executable(measure_hamming256_linear newHamming256/main.cpp)
target_compile_options(measure_hamming256_linear PRIVATE -O3 -DNDEBUG)

################################################################################
