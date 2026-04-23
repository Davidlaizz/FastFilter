# newfilter256_compact：超大输入量与容量/重复统计改造规划

## 目标

- 不限制输入总量 `N`（可大于 1600 万）。
- 保持当前去重判定逻辑不变。
- 重点保证：当真实可写入容量到顶后，系统继续处理输入并准确统计未写入原因。

## 规划内容

### 1) 去掉前置 N 拦截

- 删除“`N > 0xFFFFFF` 直接退出”的前置判断。
- benchmark 在超大 `N` 下仍可继续执行。

### 2) 保留当前真实写入上限语义

- 当前结构仍受 `id24` 约束（真实写入上限约 16,777,215）。
- 达到上限后不崩溃、不提前退出。
- 后续输入继续参与判定，并进入“拒绝统计”。

### 3) 新增/强化统计字段

- `add_attempts_total`：总尝试次数
- `insert_success_total`：实际写入成功数
- `reject_duplicate_total`：因重复被拒绝数
- `reject_capacity_total`：因容量上限被拒绝数
- `dup_match1/2/3/4`：按匹配段数统计的重复拒绝（保留）

### 4) 按轮输出统计趋势

- 每轮输出上述统计（建议包含累计值，必要时附加当轮增量）。
- 便于定位：
  - 哪一轮开始出现容量拒绝；
  - 重复拒绝何时开始显著增长。

### 5) 结果文件与可视化

- 在现有 `fill/confusion` 输出中补充容量拒绝统计。
- 增加一张趋势图：  
  `Attempts vs Inserted vs DuplicateRejected vs CapacityRejected`

### 6) 判读标准

- 当 `insert_success_total` 接近上限后，`reject_capacity_total` 应显著上升。
- 若输入重复度高，`reject_duplicate_total` 应更早上升。
- 通过两条曲线先后关系可区分“容量瓶颈”与“重复瓶颈”。

