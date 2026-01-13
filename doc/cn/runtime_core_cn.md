# 运行时核心架构 (Runtime Core Architecture)

运行时核心 (`src/runtime/core`) 是 EasyWork 的心脏，负责图执行引擎、节点生命周期管理和数据分发。它构建在 [Taskflow](https://taskflow.github.io/) 之上，以实现高性能的静态调度。

## 1. 执行图 (Execution Graph)

`ExecutionGraph` 类管理流水线执行的全局状态。

- **Taskflow 集成**：它持有一个 `tf::Taskflow` 对象用于构建静态计算图，以及一个 `tf::Executor` 用于运行它。
- **状态管理**：维护运行状态（`keep_running` 标志），允许节点发出停止信号（例如 Source 节点完成序列）。

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
4.  **同步**：如果启用（`SyncBarrier` 逻辑），检查输入的跨通道时间戳对齐。
5.  **类型转换**：如果上游类型与方法参数类型不匹配，应用注册的 `AnyCaster` 转换器。
6.  **调用**：执行 `MethodInvoker`。
7.  **打时间戳**：如果方法返回了值（例如 Source `forward`），确保其具有有效的时间戳（继承自输入或生成 `NowNs()`）。

## 4. 数据包系统 (Packet System)

数据使用 `Packet` 对象进行交换，它充当通用容器。
- **std::any 载荷**：以类型安全、类型擦除的方式存储实际数据。
- **共享所有权**：使用 `std::shared_ptr` 允许零拷贝扇出到多个下游节点。
- **时间戳**：携带纳秒级时间戳用于同步。
