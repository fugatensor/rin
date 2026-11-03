# Suka

一个 C++17 实现的证明器，支持类型事实、关系、合取前提、结构不等式、全称规则、匿名存在对象和结构化构造项。语法详见 [SYNTAX.md](SYNTAX.md)。

## 构建和运行

Linux / WSL 下只需要 C++17 编译器和 Make：

```sh
make
./build/suka .suka/main.suka
```

构建产物：

- `build/suka`：CLI。
- `build/libsuka.so`：动态链接库。
- `include/suka.h`：外部程序使用的 C ABI 头文件。
- `include/suka/pass.h`：静态 PASS 库的 C++ 接口（使用 CMake 构建）。

Make 构建的 CLI 链接动态库。二进制通过相对运行时路径寻找库，不依赖仓库的绝对位置，也不在程序中写死输入文件路径。CMake 构建的 PASS 静态库自带证明器。无参数运行会显示用法；传入任意 `.suka` 文件即可：

```sh
./build/suka path/to/file.suka
./build/suka --check path/to/file.suka
./build/suka --help
```

`--check` 检查语法和导入，不执行推理。编辑器可使用标准输入检查未保存的内容：

```sh
./build/suka --check --stdin --base-dir .suka < .suka/main.suka
```

安装到用户目录后即可直接使用 `suka path`：

```sh
make install PREFIX="$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
suka .suka/main.suka
```

也提供 CMake 构建和安装入口：

```sh
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build
./cmake-build/suka .suka/main.suka
```

## 示例

```suka
苏格拉底: human
柏拉图: human
0: nat
teacher(苏格拉底, 柏拉图)

all n: nat => succ(n): nat

all x: human =>
    x: mortal
    exists m => mother(m, x)

all x, y: human =>
    teacher(x, y) -> respect(y, x)

verify P1 = 柏拉图: mortal
verify P2 = respect(柏拉图, 苏格拉底)
verify P3 = exists m => mother(m, 苏格拉底)
verify P4 = succ(succ(0)): nat
verify P5 = exists m => mother(m, 柏拉图)
```

[`.suka/main.suka`](.suka/main.suka) 使用 `import facts` 和 `import rules`，自动读取同目录的 `facts.suka`、`rules.suka`。导入只写模块名，不加引号或 `.suka` 后缀。预期输出：

```text
verify P1 PASS
verify P2 PASS
verify P3 PASS
verify P4 PASS
verify P5 PASS
```

两个独立示例：

```sh
./build/suka .suka/mother.suka
./build/suka .suka/nat.suka
```

## 外部程序与 FFI

动态库暴露 C ABI；调用方不需要理解 C++ 类型。每次调用返回一个独立的结果句柄，读取完后使用 `suka_result_free` 释放。

| 接口 | 作用 |
| --- | --- |
| `suka_run_file(path)` | 读取文件、展开导入并证明 |
| `suka_run_source(source, base_directory)` | 证明 UTF-8 源码，导入相对于指定目录 |
| `suka_check_file(path)` | 只检查文件和导入 |
| `suka_check_source(source, base_directory)` | 只检查 UTF-8 源码 |
| `suka_result_status(result)` | 读取状态码 |
| `suka_result_output(result)` | 读取验证输出 |
| `suka_result_error(result)` | 读取错误或未完成说明 |
| `suka_result_free(result)` | 释放句柄及其字符串 |
| `suka_version()` | 读取库版本 |

输入参数是以 NUL 结尾的 UTF-8 字符串。输出指针归结果句柄所有，在释放句柄之前有效，不要单独 `free`。分配结果失败时返回空指针，调用方必须检查；错误信息通过结果返回，不向调用方抛出 C++ 异常。

C 调用示例见 [examples/ffi.c](examples/ffi.c)：

```sh
cc -Iinclude examples/ffi.c -Lbuild -lsuka -Wl,-rpath,'$ORIGIN' -o build/ffi-example
./build/ffi-example .suka/main.suka
```

Python `ctypes` 示例无需第三方包：

```sh
python3 examples/ffi.py .suka/main.suka
```

状态码：`0` 全部通过，`1` 存在 `FAIL`，`2` 输入错误，`3` 存在 `UNKNOWN`。

## PASS 库

`Suka::Pass` 将执行计划和缓冲区复用候选交给 `.suka` 规则证明。`checkPlan` 验证每步读取与公开输出的可用性，`checkReuse` 验证给定复用候选；只有 `Pass` 允许调用方采用。接口、规则、证明边界及 CMake 接入见 [PASS.md](PASS.md)。

## VS Code 与 LSP

[editor/](editor/) 提供 VS Code 插件和独立的 stdio LSP，支持 `.suka` 文件高亮、自动补全和编辑器右上角运行按钮。安装、配置 CLI 路径和插件测试步骤见 [editor/README.md](editor/README.md)。

插件不需要仓库的固定绝对路径。使用远程 WSL 工作区时，插件和 `suka` 应运行在相同环境中，才能直接使用 Linux 动态库和可执行文件。

仓库已提供 `.vscode/settings.json`，从项目根目录打开工作区时会使用 `./build/suka`。插件包位于 `editor/dist/suka-language-0.1.1.vsix`；安装后即可使用补全、高亮和运行按钮。

## 验证与范围

```sh
make test
make check
```

测试需要 Python 3；证明器和动态库本身没有 Python 依赖。

Python 测试从 `tests/fixtures/` 读取独立的 `.suka` 用例，常规用例的预期结果随源码存放。新增用例与预期注释格式见 [tests/README.md](tests/README.md)。

规则可以生成无限多个项或匿名对象，程序使用资源限制防止无限运行。已证出的目标输出 `PASS`；完整推导后仍未证出的目标输出 `FAIL`；达到推理上限时尚未证明的目标输出 `UNKNOWN`。当前不提供数学归纳法、否定或完整的一阶逻辑定理证明。
