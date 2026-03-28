# Prefix-Filter 代码架构与论文技术映射（中文总结）

## 1. 文档目标

这份文档总结两件事：

1. 代码架构：仓库里各模块负责什么、如何协作。
2. 论文技术：Prefix-Filter 的核心设计思想在源码中对应到哪些类和函数。

适用代码目录：`D:\woskspace\fastfilter\Prefix-Filter-main`


## 2. 项目总体结构

仓库是一个“统一实验框架 + Prefix-Filter实现 + 多种基线过滤器”的组合工程。

- `Prefix-Filter/`
  - Prefix-Filter 的一层 PD（packed dictionary）核心实现。
  - 关键文件：`min_pd256.hpp/.cpp`, `Shift_op.hpp/.cpp`
- `TC-Shortcut/`
  - 一种高性能增量过滤器实现（常作为 Prefix-Filter 的二层 spare）。
  - 关键文件：`TC-shortcut.hpp`, `tc-sym.hpp/.cpp`
- `Bloom_Filter/`
  - 多种 Bloom / blocked Bloom 变体（用于对比，也可作为二层 spare）。
- `cuckoofilter/`
  - Cuckoo filter 与 stable 变体（用于对比，也可作为二层 spare）。
- `Tests/`
  - 统一 API 包装层与基准测试逻辑。
  - 关键文件：`wrappers.hpp`, `smart_tests.hpp/.cpp`
- 顶层主程序
  - `main-perf.cpp`: 吞吐（插入/查询）实验入口
  - `main-fpp.cpp`: FPP 与有效空间实验入口
  - `main-built.cpp`: 构建时间实验入口
  - `example.cpp`: 最小使用示例


## 3. 架构分层（谁调用谁）

### 3.1 统一接口层：`FilterAPI<T>`

位置：`Tests/wrappers.hpp`

- 通过模板特化把不同过滤器统一成同一套接口：
  - `ConstructFromAddCount`
  - `Add`
  - `Contain`
  - `Remove`（若支持）
  - `get_name/get_byte_size/get_cap`
- Prefix-Filter、TC_shortcut、Cuckoo、Bloom 等都接到这层。
- 这样基准代码（`smart_tests.hpp`）不关心具体实现，只对模板 `Table` 调统一接口。

### 3.2 Prefix-Filter 组合层：`Prefix_Filter<Table>`

位置：`Tests/wrappers.hpp`（类定义在约第 597 行）

`Prefix_Filter<Table>` 是论文方案在工程中的主入口：

- L1：`pd_array`（`__m256i` 数组），每个 PD 32B。
- L2：`GenSpare`，类型为模板参数 `Table`（可替换）。
- 查询 `Find()`：
  - 先在对应 PD 判断是否需要仅查 L1；
  - 若判断“可能在溢出集合”，直接去 L2 查询。
- 插入 `Add()`：
  - PD 未满：直接写入 L1；
  - PD 已满：调用 `new_pd_swap_short`，把被挤出的项编码后插入 L2。

### 3.3 L1 结构层：`min_pd256`

位置：`Prefix-Filter/min_pd256.hpp/.cpp`

- 常量参数：
  - `QUOTS = 25`
  - `MAX_CAP0 = 25`
- 关键能力：
  - `find_core`：在 PD 内查找 `(quot, rem)`。
  - `cmp_qr1`：判断查询是否可跳过 L1 直接去 L2（论文中 Prefix Invariant 的工程化关键点）。
  - `new_pd_swap_short`：满桶插入策略（替换 + 溢出）。
  - `did_pd_overflowed` / `decode_last_quot`：读写溢出状态与编码状态位。

### 3.4 L2 spare 层：可替换增量过滤器

`Prefix_Filter<Table>` 的 `Table` 可以是多种实现，当前代码中常见：

- `TC_shortcut`（默认重点）
  - 位置：`TC-Shortcut/TC-shortcut.hpp`
  - 特征：two-choice 风格双候选桶 + SIMD 优化路径。
- `cuckoofilter::CuckooFilterStable<u64,12>`
- `SimdBlockFilterFixed<>`
- `Impala512<>`

L2 容量由 `get_l2_slots<Table>()` 估算（`Tests/wrappers.hpp`），使用经验比例 + 安全系数。


## 4. 论文技术点与源码位置映射

下表把论文中的核心思想对应到代码位置。

| 论文技术点 | 在代码里的实现方式 | 关键位置 |
|---|---|---|
| 两级结构（L1 prefix dictionary + L2 spare） | `Prefix_Filter<Table>` 同时维护 `pd_array` 与 `GenSpare` | `Tests/wrappers.hpp` (`class Prefix_Filter`) |
| 固定长度指纹分解（`quot`,`rem`） | `fixed_reduce` 生成 `qr`，`quot=qr>>8`，`rem=qr` | `Tests/wrappers.hpp` (`Find`/`Add` 内) |
| Prefix Invariant（查询时可判定是否跳过 L1） | `cmp_qr1(qr,pd)` 返回是否应优先看 L2 | `Prefix-Filter/min_pd256.hpp` (`cmp_qr1`) |
| 满桶插入时的替换与溢出转移 | `new_pd_swap_short` 处理满 PD，返回被挤出项 | `Prefix-Filter/min_pd256.hpp` (`new_pd_swap_short`) |
| 溢出元素进入第二层 | `incSpare_add` 将 `(pd_index,qr)` 编码后写入 L2 | `Tests/wrappers.hpp` (`incSpare_add`) |
| 两层联合查询保证无假阴性 | L1 命中或 L2 命中即返回真 | `Tests/wrappers.hpp` (`Find`) |
| 溢出状态编码（支持快速判断） | 溢出位 + `last_quot` 编码在 header 中 | `min_pd256.hpp` (`did_pd_overflowed`, `decode_last_quot`, `update_status`) |
| L2 可插拔设计 | 模板参数 `Table` + `FilterAPI<Table>` 特化 | `Tests/wrappers.hpp` |


## 5. 关键数据流（查询/插入）

### 5.1 查询路径

1. 哈希输入键，得到 `pd_index` 与 `qr(quot,rem)`。
2. 调用 `cmp_qr1(qr, pd)`：
   - `false`：在 L1 里 `find_core(quot, rem, pd)`。
   - `true`：构造编码键 `(pd_index << 13) | qr`，去 L2 `Contain`。
3. 任一层命中即返回 `true`。

对应实现：`Tests/wrappers.hpp::Find`

### 5.2 插入路径

1. 哈希输入键，定位目标 PD。
2. 若 PD 未满：
   - 调整 header + body，插入到 L1。
3. 若 PD 已满：
   - `new_pd_swap_short` 在 L1 内执行“替换策略”；
   - 返回的“被挤出项”通过 `incSpare_add` 写入 L2。

对应实现：`Tests/wrappers.hpp::Add`, `min_pd256.hpp::new_pd_swap_short`


## 6. 基准与实验代码（对应论文实验维度）

### 6.1 吞吐（插入/查询）

- 入口：`main-perf.cpp`
- 执行函数：`Bench_res_to_file_incremental_22`
- 框架：`Tests/smart_tests.hpp`

### 6.2 假阳性率与空间效率

- 入口：`main-fpp.cpp`
- 执行函数：`FPR_test`
- 结果输出：`scripts/fpp_table.csv`

### 6.3 构建时间

- 入口：`main-built.cpp`
- 执行函数：`bench_build_to_file22`

### 6.4 可执行目标

- `CMakeLists.txt` 中定义：
  - `measure_perf`
  - `measure_fpp`
  - `measure_built`
  - `example`


## 7. 建议阅读顺序（快速上手）

1. `example.cpp`：先看最小使用方式。
2. `Tests/wrappers.hpp`：看 `Prefix_Filter<Table>` 和 `FilterAPI`。
3. `Prefix-Filter/min_pd256.hpp`：看 L1 的核心操作与状态位。
4. `TC-Shortcut/TC-shortcut.hpp`：看默认 L2 spare 机制。
5. `Tests/smart_tests.hpp` + `main-*.cpp`：看实验流程与指标口径。


## 8. 论文与代码关系的边界说明

- 论文中的理论证明（复杂概率界、渐进分析）主要体现在论文文本，不会逐条出现在代码注释里。
- 代码体现的是这些理论思想对应的工程机制：
  - 两层结构
  - prefix-based 跳转判定
  - 满桶替换 + 溢出转移
  - 可插拔 spare 以适配不同性能/空间目标
- README 明确给出论文链接与实验目标，作为代码设计来源说明。

