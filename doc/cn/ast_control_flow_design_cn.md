# EasyWork AST 控制流设计（严格模式）

## 目标
- 允许用户在构图中书写原生 Python `if/else`。
- 将控制流转换为 EasyWork 的 Taskflow 条件执行。
- 在 `Pipeline.construct()` 中强制仅允许构图逻辑。
- 对可能未定义的变量在构图阶段直接报错。

## 范围
- 自动 AST 改写：`Pipeline.construct()`
- 手动 AST 改写：`@ew.ast_transform`

该能力仅属于 Python 前端。runtime-only 的 C++ 执行只消费导出的 IR，不依赖 AST 改写。

## AST 库选择
- 使用 `gast`，便于跨 Python 版本统一 AST 结构。

## AST 入口
- `Pipeline.construct()` 在构图前自动改写。
- `@ew.ast_transform` 装饰器对其他函数进行同样改写。

## Pythonic 内部 DSL
- `ew.if_(cond)` 返回 `BranchCtx`。
- `BranchCtx.true` / `BranchCtx.false` 分支上下文。
- `BranchCtx.assign(name, value)` 记录分支赋值。
- `BranchCtx.value(name)` 获取 if 后合流结果。

## 实现说明
- 运行时分支由 C++ 控制节点实现：
  - `IfNode` 使用 Taskflow condition task 选择 true/false 分支。
  - 输入多路复用（`SetInputMux`）负责在消费者节点处将被选中的分支输出合流回单一路径。
- AST 改写会自动注入 `if_` 分支上下文与赋值，驱动多路复用逻辑。

## 改写模板
用户代码：

```python
if cond:
    y = node_a(x)
else:
    y = node_b(x)
z = node_c(y)
```

AST 改写后：

```python
with ew.if_(cond) as br:
    with br.true:
        y = node_a(x)
        br.assign("y", y)
    with br.false:
        y = node_b(x)
        br.assign("y", y)

y = br.value("y")
z = node_c(y)
```

## 变量流分析规则
- 每个作用域维护 `defined` 集合。
- 遇到 `if`：
  - 计算 `defined_true`、`defined_false`。
  - `defined_out = defined_true ∩ defined_false`。
  - `maybe_undef = (defined_true ∪ defined_false) - defined_out`。
- if 后续读取 `maybe_undef` 中变量会在构图阶段报错。

## 条件类型规则

- `if` 条件必须为 `bool` 或 `int`（含 int64）。
- 非法条件类型会在构图期报错。

## 嵌套 If 支持

- 通过分支上下文栈支持嵌套 `if/else`。
- 嵌套分支内创建的节点会注册到所有上层分支。

## 构图严格限制
`construct()` 内允许：
- 节点创建。
- 节点调用 / 方法调用。
- `if/else`（条件必须是图条件）。
- 图输出赋值。

禁止：
- 纯 Python 计算（如 `a = 1 + 2`）。
- IO / print。
- `return` / `break` / `continue` / `yield`。
- 非图语义的推导式、字面量构造。
- 任何非图表达式。

## 图表达式识别（严格）
`is_graph_expr(expr)` 必须为真才能用于赋值 RHS。
严格放行：
- 直接调用节点 / 模块工厂。
- 能解析为 EasyWork 节点输出的调用链。
- 仅包含 Symbol/Node 输出的表达式。

其他全部报错。

## 错误策略
- 报错发生在构图阶段（非运行时）。
- 格式示例：

```
[EasyWork AST] Variable 'y' may be undefined after if-block at line 23
```

## 暂不支持（初期）
- `break` / `continue` / `return`。
- 超出 `if/elif/else` 的复杂控制流。

## 测试场景
- if/else 两分支都赋值同名变量 -> OK。
- 仅一分支定义变量，if 后使用 -> 报错。
- 嵌套 if 混合定义。
- `construct()` 出现非图语句 -> 报错。
- `@ew.ast_transform` 装饰普通函数 -> 生效。
- 非图条件的 if -> 不改写。
