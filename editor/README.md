# Rin for VS Code

提供 `.rin` 语法高亮、片段、补全、语法诊断和编辑器右上角运行按钮。插件内置一个通过标准输入输出通信的 LSP 服务，无 npm 运行依赖；VS Code 使用自带的 Node 运行它。

## 安装和配置

1. 在 VS Code 的扩展页面选择 **Install from VSIX…**，安装 `dist/rin-language-0.1.1.vsix`。也可运行 `code --install-extension dist/rin-language-0.1.1.vsix`。
2. 将编译好的 `rin` 加入 PATH，或配置 `rin.executable`。
3. 打开 `.rin` 文件，点击右上角的 ▶ **Run Rin File**。程序先保存文件，再在任务终端运行。

从项目根目录打开工作区时，可以这样配置：

```json
{
  "rin.executable": "./build/rin",
  "rin.diagnostics.enable": true
}
```

可执行文件配置接受 PATH 命令、绝对路径或相对第一个工作区目录的路径；不要在其中附加命令参数。实际执行通过 `ProcessExecution` 和独立参数数组完成，路径中的空格不会被解释成 shell 命令。

使用 WSL 编译的程序时，在 VS Code **Remote – WSL** 工作区中安装和运行此扩展。Windows 原生工作区需要 Windows 可执行文件。未信任的工作区只启用高亮；信任后才启动 LSP 和编译器。

## 支持的语法

```rin
import facts

all x,y:human =>
    teacher(x,y) -> respect(y,x)

all x:human => exists m => mother(m,x)
verify P1 = exists m => mother(m,苏格拉底)

0:nat
all n:nat => succ(n):nat
verify P2 = succ(succ(0)):nat
```

导入写为 `import facts`，自动读取同目录 `facts.rin`；不写引号和后缀。输入 `import ` 后可补全同目录模块名。

补全包括 `all`、`exists`、`verify`、`import` 等关键字，`=>`、`->`、`&` 等符号，以及当前文件和递归同目录导入中的对象、变量、类型、关系和构造项。支持中文名称，也会读取编辑器中尚未保存的导入文件。片段输入前缀包括 `all`、`all2`、`exists`、`verify`、`verifyexists`、`import`、`nat`。

高亮使用 TextMate 和文档语义标记：中文/ASCII 对象、量词变量、类型、关系/构造项、验证标签、数字、注释和导入模块都有对应分类。`all` 块与 `exists` 单结论中的变量使用会跟随绑定声明着色；关系和构造项共用函数颜色。Dark+ 和 Light+ 已验证对象、变量、类型、函数使用不同颜色，最终配色仍遵循当前主题和用户设置，不会更换主题。

补全由 LSP 返回，是轻量符号索引，不执行定理推理，也不提供重命名或定义跳转。

## 诊断

编辑停顿后，LSP 将当前未保存文本交给：

```text
rin --check --stdin --base-dir 当前文件目录
```

只解析和检查语法，不运行证明。`<source>:行号: 信息` 显示在当前文档，导入文件的错误显示在对应文件。`rin.diagnostics.enable` 可关闭检查；找不到编译器时会显示配置提示，补全仍可使用。

编译器读取导入文件的磁盘内容，因此检查涉及的导入文件时应先保存这些文件；当前文件无需先保存。检查超过 5 秒会被取消，旧文档版本的诊断不会覆盖新版本。

## 独立 LSP

安装 Node.js 18 或更新版本后，可在其他 LSP 客户端中配置：

```sh
node server/server.js
```

服务使用 UTF-8 `Content-Length` JSON-RPC 帧、UTF-16 文档位置，支持初始化/关闭、全文文档同步、补全、推送诊断和文件变更通知。初始化选项：

```json
{
  "executable": "rin",
  "diagnostics": { "enable": true }
}
```

标准输出仅承载协议消息，运行日志写入标准错误。

## 测试与打包

在 `editor` 目录运行：

```sh
node --test tests/*.test.js
python3 scripts/package.py
```

可选的真实编译器集成测试：

```sh
RIN_TEST_CLI=../build/rin node --test tests/protocol.test.js
```

测试覆盖 UTF-8 分帧、LSP 生命周期、导入与未保存内容补全、检查器参数和诊断更新。模拟检查器的两项测试在 Windows 跳过，可设置 `RIN_TEST_CLI` 检查实际编译器。打包只用 Python 标准库，生成并校验包含清单和运行文件的 VSIX；不发布到 Marketplace。

真实高亮测试额外使用开发工具 `vscode-textmate`、`vscode-oniguruma`。可将它们安装在临时目录，通过 `NODE_PATH` 指向其 `node_modules`，并用 `RIN_VSCODE_THEME_DIR` 指向 VS Code 的 `theme-defaults/themes` 目录，再运行上述 Node 测试。未提供这些测试工具时，对应真实分词和主题测试会跳过；插件运行不依赖这些 npm 包。

接口依据：[VS Code 扩展贡献点](https://code.visualstudio.com/api/references/contribution-points)、[VS Code API](https://code.visualstudio.com/api/references/vscode-api)、[LSP 3.17](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)、[VSIX 清单](https://learn.microsoft.com/en-us/visualstudio/extensibility/vsix-extension-schema-2-0-reference)。
