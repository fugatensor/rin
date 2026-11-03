# 测试

在项目根目录运行 `make test`。Python 只负责执行和断言，Suka 输入保存在独立的 `.suka` 文件中。

- `fixtures/prover/cases/`：证明器用例，自动遍历执行；新增文件即可增加一个用例。
- `fixtures/prover/`：CRLF 等需要特殊执行方式的用例。
- `fixtures/cli/`：按项目组织的导入用例，测试时复制到临时目录，验证相对路径、含空格目录和错误位置。
- `fixtures/ffi/`：C ABI 的源码输入及导入项目。通过读取文件测试源码接口，不在 Python 中内嵌语言代码。

预期结果写在用例末尾，避免改变语法错误的原始行号：

```text
# expect-status: 3
# expect-output: verify KNOWN PASS
# expect-output: verify PENDING UNKNOWN
# expect-error: (?i)limit
```

这些字段彼此独立：状态码默认为 `0`，输出默认为空；`expect-output` 可重复，每项对应一行标准输出。`expect-error` 是可选的标准错误正则；未指定时，状态 `0/1` 要求标准错误为空，`2/3` 要求有错误或未完成说明。

预期结果应人工确认，不能根据证明器当前输出自动更新。错误输入也直接保存在 `.suka` 文件中，不通过 Python 字符串拼接生成。

`support.py` 提供统一的执行、文件读取、临时项目复制与断言。CRLF 用例保留原始字节，并由 `.gitattributes` 禁止换行转换。数据驱动测试使用 `subTest` 显示具体文件名；测试方法数量不等于源码用例数量。
