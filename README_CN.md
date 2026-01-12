# EasyWork 项目文档

## 1. 项目概述

EasyWork 是基于 Taskflow 静态调度的计算图执行框架，旨在提供高性能、类型安全且易于扩展的异构计算流构建能力。

主要特性：
- **C++ 运行时**：高性能节点系统与调度引擎，基于 Taskflow 实现静态图调度。
- **Python API**：简洁的数据流图构建接口，支持类型检查与自动拓扑构建。
- **类型系统**：`Value` 类型擦除容器与 `TypeInfo` 反射机制，支持编译期与运行时的类型安全。
- **异构方法支持**：节点支持多个具有不同签名的输入/控制方法（C++20 反射机制）。
- **扩展性**：通过 C++20 工厂模式与宏机制，轻松注册新节点。

## 2. 项目结构

```
prototype/
├── src/                    # C++ 源代码
│   ├── runtime/            # 运行时核心
│   │   ├── core.h          # 核心执行引擎 (Taskflow 集成、BaseNode 实现)
│   │   ├── type_system.h   # 类型系统 (TypeInfo, Value, Packet)
│   │   ├── node_registry.h # 节点注册机制与 C++20 工厂模式
│   │   ├── macros.h        # 宏定义 (EW_ENABLE_METHODS, EW_REGISTER_NODE)
│   │   ├── modules.h       # 模块聚合头文件
│   │   └── memory/         # 内存管理
│   │       └── frame.h     # FrameBuffer 数据结构 (支持 Python Buffer Protocol)
│   ├── bindings/           # Python 绑定
│   │   └── bindings.cpp    # Pybind11 绑定代码
│   └── module/             # 节点实现示例
│       └── example_typed_nodes.h
├── python/                 # Python 包
│   └── easywork/
│       ├── __init__.py     # 主要 API (Pipeline, NodeWrapper)
│       └── *.so            # 编译后的 C++ 扩展
├── tests/                  # 测试文件
├── extern/                 # 外部依赖 (Taskflow 等)
└── CMakeLists.txt          # 构建配置
```

## 3. 架构与执行模型

### 3.1 调度引擎：Taskflow

项目使用 **Taskflow** 作为底层调度引擎：
- **静态图优化**：在 `Pipeline.run()` 时一次性构建 Taskflow 图，运行时调度开销极低。
- **并发执行**：利用 Taskflow 的 `executor` 自动管理线程池与任务依赖。

### 3.2 节点生命周期

每个节点继承自 `BaseNode`，拥有完整的生命周期：
1. **Build**：在 Taskflow 图中创建 `tf::Task`。
2. **Connect**：建立任务间的 `precede/succeed` 依赖关系。
3. **Activate**：激活节点（如 Source 节点准备数据）。
4. **Open(args, kwargs)**：资源初始化（用户可重写）。
5. **Process**：数据处理循环（由框架自动调度的 `RunDispatch` 或 `RunSourceLoop`）。
6. **Close()**：资源释放（用户可重写）。

### 3.3 四阶段执行流 (Pipeline.run)

Python 端的 `Pipeline.run()` 自动化了以下过程：
1. **Reset & Trace**：清理旧的连接状态，执行用户定义的 `construct()`，确定节点连接拓扑。
2. **Build**：在 Taskflow 图中实例化 C++ 节点任务。
3. **Connect**：建立 Taskflow 任务间的依赖边。
4. **Execute**：启动 Taskflow Executor 执行计算图。

> 注：`Pipeline.validate()` 是一个独立的可选步骤，用于在运行前进行静态类型检查。

### 3.4 调度与缓冲控制

每个节点支持精细化的输入方法调度配置：

- **方法排序**：通过 `set_method_order(["left", "right", "forward"])` 自定义输入端口的检查顺序。默认 `forward` 最后执行。
- **输入同步**：通过 `set_method_sync(method, True)` 开启时间戳对齐，仅当所有输入端口具有相同时间戳的数据时才触发方法。
- **FIFO 缓冲**：通过 `set_method_queue_size(method, size)` 限制输入缓冲长度。

## 4. 类型系统与数据流

### 4.1 TypeInfo 与类型安全

框架利用 C++ RTTI 和模板元编程生成 `TypeInfo`。Python 端的 `Pipeline.validate()` 会查询 C++ 节点的元数据（MethodMeta），检查上游输出类型是否与下游输入参数类型严格匹配。

### 4.2 Packet 与时间戳

数据在节点间以 `Packet` 形式传递：
- **Payload**：承载任意类型的 `Value`（支持基础类型、std::vector、自定义结构体等）。
- **Timestamp**：纳秒级时间戳，用于 `SyncBarrier` 等同步机制。

### 4.3 自动 Tuple 处理

- **C++ 端**：使用 `RegisterTupleType<std::tuple<...>>()` 注册 Tuple 类型。
- **Python 端**：支持原生解包 `a, b = node.read()`。框架会自动插入 `TupleGetNode`，实现无需额外编码的拆分。

### 4.4 自动类型转换 (Automatic Type Conversion)

EasyWork 集成了 `pybind11` 的强大类型转换机制，支持 Python 对象与 C++ 类型之间的全自动双向转换，无需手动编写转换代码。

- **复杂类型支持**：除了基础类型 (`int`, `float`, `str`)，现在可以直接传递 Python `list` 到 C++ 的 `std::vector`，传递 `dict` 到 `std::map` 等。
- **自定义对象**：只要 C++ 类通过 `pybind11` 注册，Python 对象即可自动转换为对应的 C++ 实例。

**示例：传递 List 到 C++ Vector**

C++ 定义：
```cpp
class VectorSum : public BaseNode<VectorSum> {
public:
    int forward(std::vector<int> vec) {
        return std::accumulate(vec.begin(), vec.end(), 0);
    }
    EW_ENABLE_METHODS(forward)
};
```

Python 调用：
```python
node = ew.module.VectorSum()
# Python list [1, 2, 3] 自动转换为 std::vector<int>
result = node([1, 2, 3]) 
print(result) # 输出: 6
```

## 5. C++ 节点开发

### 5.1 定义节点 (新版语法)

继承 `BaseNode<Derived>`，不再需要在模板参数中声明输入输出类型。使用 `EW_ENABLE_METHODS` 宏自动导出方法并生成反射信息。

```cpp
#include "runtime/core.h"
#include "runtime/node_registry.h"

using namespace easywork;

class MyMathNode : public BaseNode<MyMathNode> {
public:
    // 方法 1: 主处理逻辑 (int -> int)
    int forward(int input) {
        return input * 2;
    }

    // 方法 2: 配置方法 (float -> void)
    void set_scale(float scale) {
        // ... 更新内部状态
    }

    // 必须显式导出所有需暴露给 Python 的方法
    EW_ENABLE_METHODS(forward, set_scale)
};
```

### 5.2 异构方法支持

节点可以包含多个签名完全不同的方法，框架会自动处理参数解包与类型转换：
- `forward(Image img)`
- `config(std::string key, int value)`
- `reset()`

框架会自动生成调用胶水代码 (Invoker)，将 `std::vector<Packet>` 转换为原生 C++ 参数调用对应方法。

### 5.3 动态注册

使用 `EW_REGISTER_NODE` 宏注册节点，使其可以在 Python 中创建。支持定义构造函数参数（参数名与默认值）。

```cpp
// 注册 MyMathNode，名为 "MyMath"，无参数
EW_REGISTER_NODE(MyMathNode, "MyMath")

// 注册带参数的节点
class Scaler : public BaseNode<Scaler> {
public:
    Scaler(int factor) : factor_(factor) {}
    int forward(int x) { return x * factor_; }
    EW_ENABLE_METHODS(forward)
private:
    int factor_;
};

// 注册时指定参数名 "factor" 和默认值 1
EW_REGISTER_NODE(Scaler, "Scaler", Arg("factor", 1))
```

## 6. Python API 手册

### 6.1 定义 Pipeline

```python
import easywork as ew

class MyPipeline(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.src = ew.module.NumberSource(start=0, max=10)
        self.proc = ew.module.MyMath()
        self.sink = ew.module.NumberSink()

    def construct(self):
        # 默认连接到 forward 方法
        self.proc(self.src())
        
        # 连接到特定方法
        self.proc.set_scale(ew.module.ConfigProvider().read())
        
        # 链式连接
        self.sink(self.proc.read())
```

### 6.2 运行与监控

`Pipeline.run()` 具有高度鲁棒性，每次运行前会自动清理旧状态并重新构建拓扑。

```python
pipeline = MyPipeline()
pipeline.open()

# 可选：进行静态类型检查
# pipeline.validate() 

# 自动构建拓扑并执行
# 即使多次调用，每次也会重置状态，确保无副作用
pipeline.run() 

pipeline.close()
```

### 6.3 性能监控接口

- `ew.get_method_dispatch_counts()`：获取方法调度统计（调试用）。
- `ew.get_small_tracked_live_count()`：监控对象生命周期。

### 6.4 即时执行模式与图构建模式 (Eager vs Tracing)

EasyWork 支持两种操作模式，根据上下文自动切换：

1.  **Eager Mode (即时执行)**：在没有 Pipeline 上下文时，调用节点方法会直接执行对应的 C++ 函数并返回结果。适合调试和单元测试。
2.  **Tracing Mode (图构建)**：在 `construct()` 方法内或 `with pipeline:` 块内，调用节点方法会记录拓扑连接并返回 `Symbol`。

```python
import easywork as ew

multiplier = ew.module.MultiplyBy(factor=3)

# --- Eager Mode (直接调用) ---
# 直接执行 C++ 逻辑，返回计算结果
result = multiplier(10) 
print(f"Result: {result}")  # 输出: 30

# --- Tracing Mode (构建图) ---
pipeline = ew.Pipeline()
with pipeline:
    # 显式进入构图模式
    src = ew.module.NumberSource(start=1, max=5)
    # 此时返回的是 Symbol，而不是数值
    symbol = multiplier(src.read())
    
pipeline.run()
```

## 7. 高级特性

### 7.1 SyncBarrier

`SyncBarrier` 是一个特殊的内置节点，用于多路输入的时间戳对齐。它会缓存多个端口的输入，直到找到时间戳在容差范围内的一组数据，打包成 Tuple 输出。

```cpp
// C++ 侧使用
auto barrier = std::make_shared<SyncBarrier<int, float>>(1000000); // 1ms 容差
```

### 7.2 Zero-Copy FrameBuffer

提供 `FrameBuffer` 结构，支持 Python Buffer Protocol。可以在 C++ 中通过 OpenCV 处理图像，直接将数据指针暴露给 Python (NumPy)，避免内存拷贝。

## 8. 构建与环境

- **编译器**：支持 C++20 (GCC 10+, Clang 12+)
- **依赖**：Taskflow, OpenCV (可选), pybind11
- **构建**：
  ```bash
  mkdir build && cd build
  cmake ..
  make
  ```

## 9. 未来规划 (Roadmap)

- **AST 解析与原生控制流**：实现 AST 分析（类似于 Numba/Triton），以支持在构图中使用原生的 Python `if/else`, `for/while` 语法，替代临时的 `ew.If(...)` 语法糖。
- **分布式执行**：扩展 Taskflow 集成，支持跨多节点的分布式图执行。
- **可视化增强**：提供工具可视化生成的执行图，便于调试和优化。
