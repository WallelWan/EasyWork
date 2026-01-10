# EasyWork 原型演进路线图：从 TBB 封装到工业级流框架

## 阶段一：高性能内核重构 (调度引擎替换 & 热路径优化)
**核心目标**：彻底移除 TBB 依赖，切换至更适合嵌入式静态图调度的 **Taskflow** 引擎；同时通过“去字符串化”和“通用事件循环”机制，实现零开销的运行时分发。

### 1.1 数据结构与编译期优化 (De-stringification)
- [ ] **数据结构改造**
  - 修改 `MethodTaggedValue` (在 `core_tbb.h`)：将 `std::string method` 替换为 `size_t method_id`。
- [ ] **编译期哈希**
  - 利用 `constexpr hash_string` (在 `node_registry.h`)。
  - 修改 `EW_EXPORT_METHODS` 宏 (在 `macros.h`)，在编译期计算方法名的 Hash ID。
  - 在 Node 基类中建立 `std::vector<PortInfo>` 映射表，记录端口索引与 Method ID 的绑定关系，以及该方法是 Control（控制）还是 Process（数据）。

### 1.2 调度引擎替换 (TBB -> Taskflow)
> **决策理由**：
> 1. **Header-Only**：Taskflow 无需编译动态库，彻底解决瑞芯微/地平线等嵌入式平台的交叉编译依赖噩梦。
> 2. **静态图优化**：EasyWork 的图拓扑在运行前已确定，Taskflow 的静态调度比 TBB 的动态窃取（Work Stealing）更高效且无抖动。
> 3. **条件任务**：Taskflow 原生支持 `Condition Task`，天然适配复杂的业务流控制。

- [ ] **依赖清理**
  - 移除 `CMakeLists.txt` 中的 TBB 链接，引入 Taskflow 头文件。
  - 链接 `pthread` 库。
- [ ] **图构建映射 (Python -> C++)**
  - 保持 Python 层 `construct` 逻辑不变（Lazy Evaluation）。
  - 在 C++ `run()` 阶段，将原本的 TBB `make_edge` 逻辑替换为 `tf::Task::precede`。

### 1.3 优雅的并发分发机制 (Generic Event-Loop Task)
**目标**：解决 "Control vs Process" 的并发冲突，实现**用户无感知**的线程安全。

- [ ] **实现通用分发内核 (The Kernel)**
  - 不再为每个节点生成特定的代码，而是编写一个通用的 Taskflow Lambda，实现隐式序列化：
    ```cpp
    tf.emplace([node](){
        auto* cc = node->GetContext();
        
        // --- 1. 优先级排序 (自动序列化，无需用户加锁) ---
        // 策略：优先执行所有 Control 端口 (如 set_threshold, left, right)
        // 即使物理上同时到达，逻辑上也先更新参数
        for(auto& port : node->control_ports_) {
             if(cc->HasInput(port)) {
                 // 通过 ID 极速调用函数指针
                 node->CallMethod(port.id, cc->Input(port));
             }
        }
        
        // --- 2. 执行主逻辑 (Process) ---
        // 只有所有控制参数都更新完毕后，才处理核心数据流
        if(node->IsReady(cc)) {
            node->Process(cc);
        }
        
        // --- 3. 清理上下文 ---
        cc->Clear();
    });
    ```
  - **效果**：用户依然像以前一样写代码 (`node.left(val)` / `node.forward(img)`)，但框架底层自动保证了同一节点的配置修改和数据处理是顺序执行的，杜绝了 Race Condition。

---

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