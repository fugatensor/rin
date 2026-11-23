# Rin C++ 证明接口

`Rin::Pass` 是 C++17 头文件库。包含 [`rin/pass.h`](include/rin/pass.h) 后，可直接编译规则并提交事实与证明目标。Rin 负责通用推理；调用方维护领域规则、事实来源和证明义务。

## 第一次调用

先用 `run` 执行一段完整规则：

```cpp
#include <rin/pass.h>

auto decision = rin::pass::run(R"rin(
alice: person
all x: person => known(x)
verify PERSON = known(alice)
)rin");
```

`decision.verdict` 为 `Pass`、`Fail`、`Unknown` 或 `Error`，`explanation` 包含目标结果或错误位置。接受候选方案时检查 `verdict == Verdict::Pass`。

## 复用规则

重复证明使用 `Program`。构造时解析规则并编译匹配布局；`check` 接收结构化事实与目标，复用编译结果。每次调用分别推理：

```cpp
using namespace rin::pass;
const Program rules("all x: person => known(x)\n");

const Term alice{"alice"};
auto decision = rules.check(
    {{"person", {alice}}},
    {{{"known", {alice}}, "PERSON"}});
```

`Term` 表示符号、构造项或通用数值 `Number`（支持 `std::int64_t`、`std::uint64_t`、精确十进制数 `Term::decimal(...)` 与有限 `double`/`float`）。`Fact` 包含谓词与参数列表；`Obligation` 包含目标事实、诊断名称和可选查询变量列表。类型标签是普通一元谓词。同一个数值可同时出现在多个类型域中，且 `Term{1}`、`Term{std::uint64_t{1}}`、`Term{1.0}` 与 `Term::decimal("1.00")` 具有同一数学值。

`Program` 构造时报告语法异常。对象可复制或移动；`check` 与 `solve` 的推理状态属于本次调用，同一规则对象可供多个线程使用。`check` 成功时 `explanation` 为空；失败时给出目标名称。调用方可按完整输入与规则版本缓存成功结果，并将接受后的计划保存在自己的执行对象中。

## 证明数值边界

`less`、`less_equal`、`greater` 和 `greater_equal` 直接比较通用数值项（有符号 `int64_t`、无符号 `uint64_t`、精确十进制数与 `double` 按精确数学大小比较）。调用方提交原始数值，规则表达要验证的关系：

```cpp
const Program bounds(R"rin(
all x: item, a: number, b: number =>
    interval(x, a, b) & less_equal(a, b) & greater(b, -1.5) -> ordered(x)
)rin");
const Term item{"batch"}, first{std::int64_t{-1}}, last{2.5};
auto decision = bounds.check(
    {{"item", {item}}, {"number", {first}}, {"number", {last}},
     {"interval", {item, first, last}}},
    {{{"ordered", {item}}, "INTERVAL"}});
```

内建比较用于规则前提或验证目标，由证明器求值。结构化事实中的内建比较断言返回 `Error`。非有限浮点数（`NaN`、`Inf`）在构造 `Number` / `Term` 时抛出异常。规则语法和结果含义见 [SYNTAX.md](SYNTAX.md)。

## 查询模式、见证提取与推导溯源

除返回布尔判定的 `check` / `run` 外，`Program::solve` 与 `rin::pass::solve` 支持饱和推导后的实例查询、存在量词见证提取和简明推导溯源：

```cpp
using namespace rin::pass;
const Program rules(R"rin(
all a, b: drug, e: effect, d: dose =>
    co_prescribed(a, b) & different(a, b) & triggers(a, e, d) & greater(d, 0.5) -> ddi(a, b, e)
)rin");

const auto sol = rules.solve(facts); // 默认饱和推导并记录溯源
auto ddi_facts = sol.query_predicate("ddi", 3);
auto ddi_bindings = sol.query_bindings("ddi", {"A", "B", "E"});
auto bleeding = sol.query("exists A, B => ddi(A, B, bleeding)");
if (!ddi_bindings.empty()) {
    const std::string why = ddi_bindings[0].trace.format();
}
```

- `Solution::query_predicate(pred [, arity])`：按谓词名（及可选元数）提取所有已证出的基项实例；`derived_facts(...)` 和 `is_derived(fact)` 区分规则推导事实与输入给定事实。
- `Solution::query_bindings(pred, vars)`、`Solution::query(pattern, vars)` 与 `Solution::query(rin_query)`：按模式提取全部匹配绑定列表（`WitnessMatch::bindings`、`fact` 与 `trace`）。注意字符串形式使用 `exists A, B => ...` 声明变量，若模式语法非法则抛出 `std::runtime_error`。
- `Solution::queries`（与 `find_query(name)`）：对每个 `verify` 或 `Obligation` 返回 `QueryReport`，在 `Verdict::Pass` 时附带存在量词的见证绑定及拓扑有序的 `ProofTrace`（包含前提步骤、命中的内建比较条件和存在见证）。可通过 `SolveOptions` 控制是否全量饱和（`saturate`）、提取全部或首个见证（`all_matches`）以及是否记录溯源（`trace`）。

## 构建接入

作为源码子项目使用：

```cmake
add_subdirectory(path/to/rin)
target_link_libraries(my_validator PRIVATE Rin::Pass)
```

也可安装后使用 `find_package(Rin CONFIG REQUIRED)`：

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
cmake --install build/cmake --prefix /path/to/rin-sdk
```

`Rin::Pass` 是 CMake `INTERFACE` target，传递头文件目录与 C++17 要求。顶层构建默认生成独立 CLI。`RIN_BUILD_SHARED=ON` 额外生成跨语言调用使用的 C ABI 共享库；子项目的 CLI、共享库、测试和安装选项默认关闭。CLI、嵌入接口与 C ABI 共用头文件证明核心。