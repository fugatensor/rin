# SUKA PASS 库

`Suka::Pass` 是 C++17 静态库，封装 SUKA 证明器的源码入口。调用方提供 UTF-8 规则、事实和 `verify` 目标，`suka::pass::run` 返回 `Pass`、`Fail`、`Unknown` 或 `Error`，并在 `explanation` 中保留逐项结果或错误说明。公开接口位于 [`include/suka/pass.h`](include/suka/pass.h)。

```cpp
#include <suka/pass.h>

auto decision = suka::pass::run(R"suka(
alice: person
all x: person => known(x)
verify PERSON = known(alice)
)suka");
if (decision.verdict != suka::pass::Verdict::Pass) {
    // decision.explanation 给出未证成或出错的目标。
}
```

调用方负责列出全部证明义务，并将自己的领域事实和规则放入源码。没有 `verify` 目标的源码会得到 `Pass`。`Fail` 表示有限规则推导未证成目标，`Unknown` 表示达到资源上限，`Error` 表示输入或运行错误。决策代码应仅接受 `Pass`。`run` 会拒绝内嵌 NUL 字节，避免 C ABI 截断源码。

## 构建与接入

```sh
cmake -S . -B build/pass-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/pass-cmake
ctest --test-dir build/pass-cmake --output-on-failure
cmake --install build/pass-cmake --prefix /path/to/suka-sdk
```

顶层构建默认生成 CLI、C ABI 共享库和 PASS 静态库。作为 CMake 子项目时，`SUKA_BUILD_SHARED`、`SUKA_BUILD_CLI`、`SUKA_BUILD_TESTS`、`SUKA_INSTALL` 默认关闭，`Suka::Pass` 始终可链接。安装后可用 `find_package(Suka CONFIG REQUIRED)` 和 `Suka::Pass`；静态库包含证明器，不依赖 SUKA 动态库或源码路径。

执行计划、显存分配、图切分和 IR 改写等领域的候选生成、事实编码、规则和完整性检查由调用方维护。SUKA 只执行收到的证明程序；规则语法见 [`SYNTAX.md`](SYNTAX.md)。
