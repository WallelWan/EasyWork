# 运行时核心架构

运行时核心 (`src/runtime/core`) 是 EasyWork 的图执行引擎，基于 [Taskflow](https://taskflow.github.io/)。当前版本已完成头文件职责拆分，并让 C++/Python 节点共享同一套分发主线。

## 1. 头文件分层

`core.h` 现在是聚合入口，具体实现按职责拆分为：

- `ids.h`：方法 ID 常量与 `ErrorPolicy`
- `execution_graph.h`：`ExecutionGraph` 全局状态与错误统计
- `method_reflection.h`：方法元数据与调用器工厂
- `node.h`：类型擦除 `Node`、拓扑状态、共享分发计划/执行器
- `base_node.h`：CRTP `BaseNode<Derived>` 与快路径分发
- `executor.h`：执行循环与图生命周期辅助

## 2. ExecutionGraph

`ExecutionGraph` 负责：

- Taskflow 图与执行器
- 运行停止标志（`keep_running`、`skip_current`）
- 错误信息（`last_error`、`error_count`、`last_error_code`）
- 错误策略（`FailFast` / `SkipCurrentData`）
- 运行时图锁定状态

## 3. Node 模型

`Node` 是运行时的类型擦除接口。

- 持有拓扑、端口映射、缓冲、方法配置与输出包。
- 内部可变状态已封装，不再暴露 public 可变成员。
- 通过受控访问接口供派生类和 bindings 使用。
- `Open/Close` 可选行为由 `HasMethod(ID_OPEN/ID_CLOSE)` 决定，不再依赖异常文本匹配。

## 4. 统一分发引擎

共享分发核心在 `Node` 中：

- `BuildDispatchPlans(...)`
- `RunDispatchPlans(...)`

`BaseNode` 与 `PyNode` 都复用这套引擎。

### 4.1 BaseNode 路径

`BaseNode<Derived>` 从 `method_registry()` 构建方法计划，并保留：

- 类型安全调用路径（`MethodInvoker`）
- 针对平凡可拷贝签名的快路径（`FastInvoker`）

### 4.2 PyNode 路径

`PyNode` 使用同一分发主线，仅对以下部分做 Python 特化：

- Python 可调用对象签名解析
- Python 调用与 `py::object` 的 mux 控制值解码
- 需要 GIL 的缓冲/输出清理覆写

## 5. GraphBuild 与 GraphSpec 合同

`GraphBuild` 读取严格 GraphSpec v1：

- 必须提供 `schema_version`，且值必须为 `1`
- 边必须使用方法名（`from.method`、`to.method`）
- 旧字段 `method_id` 会被拒绝
- `method_config.sync` 已移除并拒绝
- `Connect` 会校验生产者/消费者方法存在性和消费者 `arg_idx` 范围

完整 schema 与迁移说明见 `doc/ir_schema.md`。

## 6. Runtime-only 约束

仍支持无 Python 的 runtime-only 构建：

```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
ctest --test-dir build_rt
```

可直接执行导出的 JSON 图：

```bash
./build_rt/easywork-run --graph graph.json
```

