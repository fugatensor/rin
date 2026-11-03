# Rin

一个 C++17 实现的证明器，支持类型事实、关系、合取前提、结构不等式、全称规则、匿名存在对象和结构化构造项。语法详见 [SYNTAX.md](SYNTAX.md)。

## 构建和运行

Linux / WSL 下只需要 C++17 编译器和 Make：

```sh
make
./build/rin .rin/main.rin
```

构建产物：

- `build/rin`：CLI。
- `build/librin.so`：执行 `make ffi` 后生成的可选 C ABI 动态库。
- `include/rin.h`：外部程序使用的 C ABI 头文件。
- `include/rin/pass.h`：可直接嵌入的 C++17 头文件证明接口。

CLI 直接编译头文件证明核心。传入 `.rin` 文件即可执行：

```sh
./build/rin path/to/file.rin
./build/rin --check path/to/file.rin
./build/rin --help
```

`--check` 检查语法和导入，不执行推理。编辑器可使用标准输入检查未保存的内容：

```sh
./build/rin --check --stdin --base-dir .rin < .rin/main.rin
```

安装到用户目录后即可直接使用 `rin path`：

```sh
make install PREFIX="$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
rin .rin/main.rin
```

也提供 CMake 构建和安装入口：

```sh
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build
./cmake-build/rin .rin/main.rin
```

## 示例

```rin
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

[`.rin/main.rin`](.rin/main.rin) 使用 `import facts` 和 `import rules`，自动读取同目录的 `facts.rin`、`rules.rin`。导入只写模块名，不加引号或 `.rin` 后缀。预期输出：

```text
verify P1 PASS
verify P2 PASS
verify P3 PASS
verify P4 PASS
verify P5 PASS
```

两个独立示例：

```sh
./build/rin .rin/mother.rin
./build/rin .rin/nat.rin
```

## 外部程序与 FFI

运行 `make ffi` 或设置 CMake 的 `-DRIN_BUILD_SHARED=ON` 生成动态库；Make 安装使用 `make install-ffi PREFIX=/path/to/prefix`。动态库提供 C ABI，适合跨语言调用。每次调用返回独立的结果句柄，读取完后使用 `rin_result_free` 释放。

| 接口 | 作用 |
| --- | --- |
| `rin_run_file(path)` | 读取文件、展开导入并证明 |
| `rin_run_source(source, base_directory)` | 证明 UTF-8 源码，导入相对于指定目录 |
| `rin_check_file(path)` | 只检查文件和导入 |
| `rin_check_source(source, base_directory)` | 只检查 UTF-8 源码 |
| `rin_result_status(result)` | 读取状态码 |
| `rin_result_output(result)` | 读取验证输出 |
| `rin_result_error(result)` | 读取错误或未完成说明 |
| `rin_result_free(result)` | 释放句柄及其字符串 |
| `rin_version()` | 读取库版本 |

输入参数是以 NUL 结尾的 UTF-8 字符串。输出指针归结果句柄所有，在释放句柄之前有效，不要单独 `free`。分配结果失败时返回空指针，调用方必须检查；错误信息通过结果返回，不向调用方抛出 C++ 异常。

C 调用示例见 [examples/ffi.c](examples/ffi.c)：

```sh
cc -Iinclude examples/ffi.c -Lbuild -lrin -Wl,-rpath,'$ORIGIN' -o build/ffi-example
./build/ffi-example .rin/main.rin
```

Python `ctypes` 示例无需第三方包：

```sh
python3 examples/ffi.py .rin/main.rin
```

状态码：`0` 全部通过，`1` 存在 `FAIL`，`2` 输入错误，`3` 存在 `UNKNOWN`。

## PASS 库

`Rin::Pass` 是可直接内联的 C++17 头文件库。`Program` 解析一次规则，通过 `check` 接收结构化事实和目标；`run` 用于执行完整 `.rin` 文本。数值项支持 `uint64_t`，`less` / `less_equal` 比较原始值。渐进接入说明见 [PASS.md](PASS.md)。

## VS Code 与 LSP

[editor/](editor/) 提供 VS Code 插件和独立的 stdio LSP，支持 `.rin` 文件高亮、自动补全和编辑器右上角运行按钮。安装、配置 CLI 路径和插件测试步骤见 [editor/README.md](editor/README.md)。

使用远程 WSL 工作区时，插件和 `rin` 运行在相同环境中，使用对应的可执行文件。

仓库已提供 `.vscode/settings.json`，从项目根目录打开工作区时会使用 `./build/rin`。插件包位于 `editor/dist/rin-language-0.1.1.vsix`；安装后即可使用补全、高亮和运行按钮。

## 测试

```sh
make test
make check
```

测试使用 Python 3 组织输入与断言。

Python 测试从 `tests/fixtures/` 读取独立的 `.rin` 用例，常规用例的预期结果随源码存放。新增用例与预期注释格式见 [tests/README.md](tests/README.md)。

规则可以生成无限多个项或匿名对象，程序使用资源限制防止无限运行。已证出的目标输出 `PASS`；完整推导后仍未证出的目标输出 `FAIL`；达到推理上限时尚未证明的目标输出 `UNKNOWN`。推理规则和资源上限见 [SYNTAX.md](SYNTAX.md#结果与退出码)。
