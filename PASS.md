# SUKA PASS 库

`Suka::Pass` 是 C++17 静态库，将执行计划和缓冲区复用候选转换成 SUKA 事实，执行仓内的形式化规则，并返回 `Pass`、`Fail`、`Unknown` 或 `Error`。调用方只在 `Pass` 时接受候选。规则源码位于 [`rules/`](rules/)；构建时嵌入静态库，安装后的调用方使用公开头文件 [`include/suka/pass.h`](include/suka/pass.h)。

## 执行计划合法性

`checkPlan` 接收逻辑缓冲区数量、公开输入/输出索引，以及按执行顺序排列的步骤输入/输出索引。每个步骤先读取输入、再写出输出。公开输入在边界 `s0` 可用，步骤 `i` 写出的缓冲区在边界 `s(i+1)` 可用。[`plan_availability.suka`](rules/plan_availability.suka) 中的规则按有限步骤展开：

```suka
all b: buffer => available(b, s0) -> available(b, s1)
```

PASS 为每个步骤实例化一条有限规则，按计划顺序传递可用性。证明器的 256 轮上限仍作为资源边界。

对每个步骤输入，PASS 查询 `available(buffer, s_i)`；对每个公开输出，查询最终边界的 `available`。因此读前写会使相应 `READ_i_j` 目标失败。输出查询使用 `OUTPUT_j` 标签。步骤顺序就是待验证的执行顺序；ASK 另行检查 kernel 参数、端口绑定与设备约束。

C++ 入口检查索引范围、公开输出存在、每个缓冲区有且只有一个写者，以及公开输入不会被步骤重写。求解器证明读取时刻与公开输出时刻的可用性。规则只使用正向事实，`Fail` 表示在这组有限事实与规则中没有证明，`Unknown` 表示资源上限阻止了结论；两种结果都不能放行计划。证明器每次运行最多推导 256 轮、10000 条事实，超出范围的计划可能得到 `Unknown`。

```cpp
#include <suka/pass.h>

suka::pass::Plan plan{
    3,
    {0},
    {2},
    {{{0}, {1}}, {{1}, {2}}}
};
auto decision = suka::pass::checkPlan(plan);
if (decision.verdict != suka::pass::Verdict::Pass) {
    // decision.explanation 包含 READ_i_j / OUTPUT_j 查询结果或结构错误。
}
```

## 缓冲区复用候选

`checkReuse` 检查同一提交内的上次使用早于下次写入，或跨提交时前一次完成事件已经结束；同时要求同一分配域、分配器拥有存储、设备一致、容量足够、公开输出已释放。ASK 等调用方负责提供实际事件状态、分配域、分配器所有权、设备、容量和执行顺序。C++ 将大小比较与顺序比较编码为独立事实，[`buffer_reuse.suka`](rules/buffer_reuse.suka) 使用合取前提组合全部条件，证明 `legal_reuse`。此接口适合在选择复用候选时调用，不能取代实际资源状态读取和事件保留。

```cpp
suka::pass::ReuseCandidate candidate{
    1, 2, 128, 64, true, false, true, true, true, true
};
auto decision = suka::pass::checkReuse(candidate);
```

## 构建与接入

```sh
cmake -S . -B build/pass-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/pass-cmake
ctest --test-dir build/pass-cmake --output-on-failure
cmake --install build/pass-cmake --prefix /path/to/suka-sdk
```

顶层构建默认生成 CLI、C ABI 共享库和 PASS 静态库。作为 CMake 子项目时，`SUKA_BUILD_SHARED`、`SUKA_BUILD_CLI`、`SUKA_BUILD_TESTS`、`SUKA_INSTALL` 默认关闭，`Suka::Pass` 始终可链接。安装后可用 `find_package(Suka CONFIG REQUIRED)` 和 `Suka::Pass`；静态库包含求解器和规则，不依赖 SUKA 动态库或源码路径。

后续图切分、算子融合与 IR 优化可各自定义候选事实和通过条件，加入独立规则包与 PASS API。候选生成、成本选择、IR 改写仍由 ASK 负责；SUKA 对给定候选证明其声明的合法性条件。
