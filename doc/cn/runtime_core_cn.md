# 运行时核心架构 (Runtime Core Architecture)

运行时核心 (`src/runtime/core`) 是 EasyWork 的心脏，负责图执行引擎、节点生命周期管理和数据分发。它构建在 [Taskflow](https://taskflow.github.io/) 之上，以实现高性能的静态调度。

## 1. 执行图 (Execution Graph)

`ExecutionGraph` 类管理流水线执行的全局状态。

- **Taskflow 集成**：它持有一个 `tf::Taskflow` 对象用于构建静态计算图，以及一个 `tf::Executor` 用于运行它。
- **状态管理**：维护运行状态（`keep_running` 标志），允许节点发出停止信号（例如 Source 节点完成序列）。
- **错误策略**：运行时支持 `FailFast`（立即停止）或 `SkipCurrentData`（丢弃当前数据继续）。
- **图锁定**：运行期间锁定图，防止连接被修改。

## 2. 节点架构 (Node Architecture)

节点系统围绕 `Node` 基类和 `BaseNode<Derived>` 模板设计，提供了一种 类型擦除 + CRTP (奇异递归模板模式) 的混合架构。

### 2.1 Node 基类
`Node` 是运行时引擎使用的类型擦除接口。
- **缓冲**：管理输入 `port_buffers_` 用于数据缓冲。
- **上游连接**：存储 `UpstreamConnection` 信息用于图依赖构建。
- **反射**：提供 `get_type_info()` 和 `invoke()` 等虚方法用于运行时内省。
- **配置**：处理特定于方法的设置，如 `SetMethodOrder`（方法顺序）、`SetMethodSync`（同步）和 `SetMethodQueueSize`（队列大小）。

### 2.2 BaseNode 模板
`BaseNode<Derived>` 使用 CRTP 实现了类型安全的逻辑。
- **任务创建**：在 `build()` 中自动创建 `tf::Task`。
- **统一分发**：所有节点（包括 Source 节点）都运行 `RunDispatch`。
- **Source 处理**：如果节点有一个无参数的 `forward` 方法，`RunDispatch` 会在调用 `forward` 时自动处理时间戳生成。

## 3. 分发机制 (Dispatch Mechanism)

EasyWork 支持 **异构方法 (Heterogeneous Methods)**，这意味着单个节点可以有多个具有不同签名的入口点（方法）。

### 3.1 调用器系统 (Invoker System)
框架为节点中每个暴露的方法生成 `MethodInvoker` 函数（类型擦除包装器）。
- **签名**：`std::function<Packet(Node*, const std::vector<Packet>&)>`
- **功能**：它接受通用的 `Packet` 列表，将其转换为方法所需的具体 C++ 类型，调用该方法，并将结果封装回 `Packet`。

### 3.2 分发逻辑 (`RunDispatch`)
`RunDispatch` 循环充当每个节点的本地调度器：
1.  **缓冲输入**：将数据从上游节点移动到本地输入缓冲区。
2.  **检查顺序**：按照配置的优先级 (`method_order_`) 遍历方法。
3.  **检查可用性**：验证缓冲区中是否有足够的数据用于方法的参数。
4.  **类型转换**：如果上游类型与方法参数类型不匹配，应用注册的 `AnyCaster` 转换器。
5.  **调用**：执行 `MethodInvoker`。
6.  **打时间戳**：如果方法返回了值（例如 Source `forward`），确保其具有有效的时间戳（继承自输入或生成 `NowNs()`）。

## 4. 数据包系统 (Packet System)

数据使用 `Packet` 对象进行交换，它充当通用容器。
- **std::any 载荷**：以类型安全、类型擦除的方式存储实际数据。
- **共享所有权**：使用 `std::shared_ptr` 允许零拷贝扇出到多个下游节点。
- **时间戳**：携带纳秒级时间戳用于同步。

## 5. 构图期约束

- **IfNode 条件类型**：条件输出必须为 `bool` 或 `int`（含 int64），否则构图期报错。
- **Mux 控制类型**：控制包必须为 `bool` 或 `int`，未映射的控制值视为错误。
- **重复运行**：Taskflow 图会重置，但节点自身状态需自行处理可重入。

## 6. GraphBuild（C++ 构建器）与 GraphSpec

EasyWork 提供 C++ 构建器，可读取 Python 导出的 JSON 图描述（GraphSpec）。

### 6.1 C++ GraphBuild

```cpp
#include "runtime/core/graph_build.h"

auto graph = easywork::GraphBuild::FromJsonFile("graph.json");
graph->Run();
```

`GraphBuild` 使用已注册的 C++ 节点构图并运行，复用相同的运行时核心。

### 6.2 GraphSpec JSON

GraphSpec 是一个包含节点、连接、Mux 路由与方法配置的 JSON 文档。

关键字段：

- `nodes`: `{id, type, args, kwargs}` 列表
- `edges`: `{from:{node_id, method}, to:{node_id, method, arg_idx}}` 列表
- `mux`: `{consumer_id, method, arg_idx, control_id, map}` 列表
- `method_config`: `{node_id, order}` / `{node_id, method, sync}` / `{node_id, method, queue_size}`

限制：

- 含 Python 节点或内部辅助节点时，导出直接失败。
- 构造参数只支持基本 JSON 类型（bool/int/float/string）。
- GraphSpec 不包含 Node open/close 参数。

## 7. Runtime-only 构建约束

runtime 路径可在无 Python/pybind11/OpenCV 的条件下构建：

```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
ctest --test-dir build_rt
```

可直接运行导出的图 JSON：

```bash
./build_rt/easywork-run --graph graph.json
```
