# SUKA C++ 证明接口

`Suka::Pass` 是 C++17 头文件库。包含 [`suka/pass.h`](include/suka/pass.h) 后，可直接编译规则并提交事实与证明目标。SUKA 负责通用推理；调用方维护领域规则、事实来源和证明义务。

## 第一次调用

先用 `run` 执行一段完整规则：

```cpp
#include <suka/pass.h>

auto decision = suka::pass::run(R"suka(
alice: person
all x: person => known(x)
verify PERSON = known(alice)
)suka");
```

`decision.verdict` 为 `Pass`、`Fail`、`Unknown` 或 `Error`，`explanation` 包含目标结果或错误位置。接受候选方案时检查 `verdict == Verdict::Pass`。

## 复用规则

重复证明使用 `Program`。构造时解析规则；`check` 接收结构化事实与目标，复用已解析的规则。每次调用分别推理：

```cpp
using namespace suka::pass;
const Program rules("all x: person => known(x)\n");

const Term alice{"alice"};
auto decision = rules.check(
    {{"person", {alice}}},
    {{{"known", {alice}}, "PERSON"}});
```

`Term` 表示符号、构造项或 `std::uint64_t` 数值；`Fact` 包含谓词与参数列表；`Obligation` 包含目标事实与诊断名称。类型标签是普通一元谓词。同一个数值可同时出现在多个类型域中。

`Program` 构造时报告语法异常。`check` 的推理状态属于本次调用，同一规则对象可供多个线程使用。成功时 `explanation` 为空；失败时给出目标名称。调用方可按完整输入与规则版本缓存成功结果，并将接受后的计划保存在自己的执行对象中。

## 证明数值边界

`less` 和 `less_equal` 直接比较 `uint64_t` 项。调用方提交原始数值，规则表达要验证的关系：

```cpp
const Program bounds(R"suka(
all x: item, a: number, b: number =>
    interval(x, a, b) & less_equal(a, b) -> ordered(x)
)suka");
const Term item{"batch"}, first{std::uint64_t{2}}, last{std::uint64_t{8}};
auto decision = bounds.check(
    {{"item", {item}}, {"number", {first}}, {"number", {last}},
     {"interval", {item, first, last}}},
    {{{"ordered", {item}}, "INTERVAL"}});
```

内建比较用于规则前提或验证目标，由证明器求值。结构化事实中的内建比较断言返回 `Error`。规则语法和结果含义见 [SYNTAX.md](SYNTAX.md)。

## 构建接入

作为源码子项目使用：

```cmake
add_subdirectory(path/to/suka)
target_link_libraries(my_validator PRIVATE Suka::Pass)
```

也可安装后使用 `find_package(Suka CONFIG REQUIRED)`：

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
cmake --install build/cmake --prefix /path/to/suka-sdk
```

`Suka::Pass` 是 CMake `INTERFACE` target，传递头文件目录与 C++17 要求。顶层构建默认生成独立 CLI。`SUKA_BUILD_SHARED=ON` 额外生成跨语言调用使用的 C ABI 共享库；子项目的 CLI、共享库、测试和安装选项默认关闭。CLI、嵌入接口与 C ABI 共用头文件证明核心。

ASK 的领域规则与验证入口见 [ASK 规则证明与计划校验](../ask/docs/规则证明与计划校验.md)。
