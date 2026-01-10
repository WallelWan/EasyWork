# EasyWork 原型演进路线图：从 TBB 封装到工业级流框架

## 阶段一：高性能内核重构 (调度引擎替换 & 热路径优化)
**目标**：彻底移除 TBB 依赖，切换至更适合嵌入式静态图调度的 **Taskflow** 引擎，同时消除运行时字符串开销，奠定高性能基础。

- [ ] **数据结构改造 (去字符串化)**
  - 修改 `MethodTaggedValue` (在 `core_tbb.h`)：将 `std::string method` 替换为 `size_t method_id`。
- [ ] **编译期哈希**
  - 利用 `constexpr hash_string` (在 `node_registry.h`)。
  - 修改 `EW_EXPORT_METHODS` 宏 (在 `macros.h`)，在编译期计算方法名的 Hash ID。
- [ ] **调度引擎替换 (TBB -> Taskflow)**
  - **依赖清理**：移除 `CMakeLists.txt` 中的 TBB 链接，引入 Header-Only 的 Taskflow 库。
  - **图容器替换**：将 `tbb::flow::graph` 替换为 `tf::Taskflow`，将 `tbb::flow::make_edge` 替换为 `tf::Task::precede`。
  - **静态图优化**：利用 Taskflow 的静态拓扑特性构建执行图，消除 TBB 动态任务窃取（Work Stealing）在固定流水线中的抖动开销。
  - **条件执行**：使用 Taskflow 的 **Condition Task** 替换原有的动态分发逻辑，原生支持 "If-Else" 业务流。
- [ ] **执行逻辑优化**
  - 在 `InvokeWithValuesAndMethods` 中，将 `if (method == "forward")` 替换为整数比较 `if (method_id == ID_FORWARD)`。
  - 结合 Taskflow 的条件任务，将原本的哈希表查找（`std::unordered_map`）转化为图的拓扑分支（Branching），进一步减少运行时开销。

## 阶段二：核心架构升级 (MediaPipe-Lite 模式)
**目标**：引入时间戳概念，解决多模态传感器数据对齐与同步问题。

- [ ] **引入 Packet**
  - 定义 `Packet` 类 (在 `type_system.h`)：
    ```cpp
    struct Packet {
        std::shared_ptr<Value> payload;
        int64_t timestamp; // 核心：纳秒级时间戳
    };
    ```
  - 替换原有的 `Value` 传递，改为在节点间传递 `Packet`。
- [ ] **实现 SyncBarrier (同步栅栏)**
  - 基于 Taskflow 实现自定义同步节点（利用 `tf::Observer` 或自定义 Task 逻辑）。
  - 实现基于时间戳对齐的同步逻辑：
    - 缓存多路输入 Packet。
    - 比较队首时间戳，丢弃过早数据。
    - 仅当多路时间戳对齐（或满足容差）时，才触发下游 Task 执行。

## 阶段三：节点生命周期重构
**目标**：支持 AI 模型加载、状态管理及复杂的 NPU 上下文交互。

- [ ] **定义 Context (上下文)**
  - 创建 `ProcessContext` 类：
    - 提供 `Input(index)` 读取接口。
    - 提供 `Output(index)` 发送接口。
    - 提供 `InputTimestamp()` 获取当前帧时间。
- [ ] **重构 Node 接口**
  - 将 `forward` 函数式调用改为生命周期方法：
    ```cpp
    virtual void Open();                  // 初始化 (加载模型)
    virtual void Process(ProcessContext* cc); // 处理 (每帧调用)
    virtual void Close();                 // 销毁 (释放资源)
    ```