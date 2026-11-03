# Suka 语法

Suka 使用 UTF-8 文本，文件扩展名为 `.suka`。每行一条语句；空行和 `#` 后的注释不参与证明。顶层语句不缩进，`all` 块内使用一致缩进，一个 Tab 按四个空格计。

## 事实与项

```suka
苏格拉底: human
柏拉图: human
史努比: dog
0: nat
teacher(苏格拉底, 柏拉图)
```

`对象: 类型` 表示一个一元事实，等价于 `类型(对象)`。关系按参数位置匹配，`teacher(a, b)` 与 `teacher(b, a)` 是不同事实。

名称可以使用中文。项可以是名称，也可以是嵌套构造项：

```suka
succ(0)
succ(succ(0))
pair(苏格拉底, 柏拉图)
```

项按结构匹配。`0` 是一个名称，不会自动执行算术；`2` 不会自动转换成 `succ(succ(0))`。也不内置 `succ` 或 `nat` 的含义，它们由事实和规则定义。

## 全称规则

```suka
all n: nat => succ(n): nat

all x: human =>
    x: mortal

all x, y: human =>
    teacher(x, y) -> respect(y, x)

all x: human, y: human =>
    teacher(x, y) & teacher(y, x) -> colleagues(x, y)
```

`all` 声明任意满足类型条件的变量。`all x, y: human =>` 等价于 `all x: human, y: human =>`；也可以写 `all x: human, p: dog =>` 混合不同类型。

`=>` 后可在同一行写一个结论，也可换行并缩进写多条规则。直接结论只依赖变量的类型条件。`前提 -> 结论` 还要求前提成立；多个前提用 `&` 连接，须同时成立。块内每行是一条独立规则，类型条件是附加前提。

变量只在当前 `all` 规则或块内有效。没有被 `all` 或 `exists` 声明的名称视作常量。重复变量必须匹配同一个项，例如 `pair(x, x)` 不会匹配 `pair(a, b)`。

`different(a, b)` 是结构不等式，可作为规则前提，与其它前提用 `&` 连接。两个项中的全称变量先由类型条件绑定；当项的名称或构造结构不同，前提成立。它也可用于没有存在变量的验证目标。`different` 不能作为事实、规则结论或类型名，因此不能伪造不等式。

```suka
first: node
second: node

all a: node, b: node =>
    different(a, b) -> distinct_nodes(a, b)

verify DISTINCT = distinct_nodes(first, second)
verify SAME = different(first, first)
```

## 存在规则与查询

```suka
all x: human =>
    exists m => mother(m, x)

all x: human =>
    chosen(x) -> exists m => mother(m, x)
    exists a, b => parents(a, b, x)
```

`exists m => mother(m, x)` 表示对每个满足前提的 x，存在某个 m，使 `mother(m, x)` 成立。程序会引入一个匿名对象，不要求输入中已经出现母亲的名字。

同一规则在同一组全称变量绑定上只生成一次对象。匿名对象是证明中的占位符：程序不会据此断言母亲叫 Alice，也不会据此断言不同人的母亲一定不同。

存在变量的作用域是当前结论或查询；不能重复声明，也不能遮蔽所在 `all` 的变量。`=>` 分隔量词声明与其描述，`->` 分隔条件与结论。

## 验证目标

标签放在 `verify` 后面，用 `=` 与目标分隔：

```suka
verify P1 = 柏拉图: mortal
verify P2 = respect(柏拉图, 苏格拉底)
verify P3 = exists m => mother(m, 苏格拉底)
verify P4 = succ(succ(0)): nat
```

输出保持一行一个结果：

```text
verify P1 PASS
```

## 文件导入

同目录文件可以互相导入：

```suka
import facts
import rules

verify P1 = 柏拉图: mortal
```

`import` 后只写裸模块名，程序自动补上 `.suka` 后缀。文件相对于**包含这条 import 的文件所在目录**解析，与运行命令时的工作目录无关。

| 导入语句 | 读取的同目录文件 |
| --- | --- |
| `import facts` | `facts.suka` |
| `import 公理` | `公理.suka` |
| `import math.base` | `math.base.suka` |

模块名允许中文和内部的点，不允许引号、`.suka` 后缀、路径分隔符、冒号、点开头或空白。因此 `import "facts.suka"`、`import facts.suka`、`import ../facts` 都是错误写法。项目目录和通过 CLI 指定的文件路径仍可包含空格，但导入的模块名不能包含空格。

导入会递归展开，整个程序的事实和规则统一推理。同一文件只加载一次，包括重复导入和菱形导入。导入中的 `verify` 也会执行，顺序为首次展开时出现的顺序。循环导入、文件缺失或导入文件语法错误会报告错误，并保留出错文件的位置。

从字符串运行时，FFI 的 `base_directory` 或 CLI 的 `--base-dir` 指定导入的起点。

## 结果与退出码

| 结果 | 含义 | 退出码 |
| --- | --- | --- |
| 全部 `PASS` | 已找到所有查询的证明 | `0` |
| 存在 `FAIL` | 当前推导已完成，仍无法推出该目标 | `1` |
| 输入错误 | 文件、导入或语法错误 | `2` |
| 存在 `UNKNOWN` | 推理达到资源上限 | `3` |

`FAIL` 不表示已证明目标的否定；同时出现 `FAIL` 与 `UNKNOWN` 时退出码为 `3`。

开放的自然数后继规则和递归存在规则可能生成无穷多个对象。当前推理上限为 256 轮、10000 条事实、每个前提匹配阶段 10000 组绑定、项深度 128，以及单条结论的项节点总数 10000。推导达到上限时，已经找到证明的目标保持 `PASS`，尚未证明且受限制影响的目标返回 `UNKNOWN`，退出码为 `3`。

输入项解析超过深度限制属于输入错误，退出码为 `2`。导入数量与递归深度也受限制。

当前不支持否定、析取、等式推理、任意嵌套量词、数学归纳法或一般算术求值；它不是完整的一阶逻辑或皮亚诺算术证明器。

名称 `0`、`1` 等仍是普通符号；`different(1, 2)` 比较项结构，不执行整数大小比较。
