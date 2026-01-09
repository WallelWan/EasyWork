# EasyWork: 高性能嵌入式 AI 图编译器 - 设计文档

## 1. 项目概述 (Project Overview)

**EasyWork** 是一个专为嵌入式 AI 应用（如无人机、机器人、自动驾驶）设计的领域特定语言（DSL）框架。EasyWork 采用 **"追踪-编译-执行"** 架构，前端 Python 定义，后端 C++ TBB 执行。

## 2. 系统架构 (System Architecture)

### 2.1 核心技术栈 (Tech Stack)

| 组件 | 技术 | 版本要求 | 说明 |
|------|------|----------|------|
| **调度引擎** | Intel OneTBB | 2021+ | 利用 `tbb::flow::graph` 实现工业级并行调度 |
| **内存管理** | 自研 FrameBuffer | - | 硬件感知设计，支持零拷贝到 NumPy |
| **图像处理** | OpenCV | 任意 | 作为算子实现库使用 |
| **绑定层** | pybind11 | v2.11.1+ | C++/Python 互操作，优先使用 pip 安装版本 |
| **日志系统** | spdlog | v1.12.0 | 源码构建以确保 ABI 兼容性 |
| **构建系统** | CMake | 3.15+ | 跨平台构建配置 |
| **C++ 标准** | C++17 | - | 现代特性支持 |

### 2.2 架构分层

```
┌─────────────────────────────────────────────────────────┐
│                   Python 用户层                          │
│              (Pipeline, Symbol, NodeWrapper)             │
└────────────────────────┬────────────────────────────────┘
                         │ Python Buffer Protocol
                         │ 零拷贝数据传输
┌────────────────────────▼────────────────────────────────┐
│                  pybind11 绑定层                         │
│         (GIL 管理, std::shared_ptr, 类型转换)            │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│                  C++ Runtime (TBB)                       │
│  ┌──────────────────────────────────────────────────┐   │
│  │  SourceNode ──► ProcessNode ──► SinkNode        │   │
│  │  (input_node)    (function_node)  (function_node)│   │
│  │                                                  │   │
│  │  延迟连接模式 (Deferred Connection Pattern)      │   │
│  │  - build(): 创建节点                             │   │
│  │  - connect(): 建立边连接                         │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## 3. 核心模块 (Core Modules)

### 3.1 C++ 运行时 (`src/runtime/`)

#### `core_tbb.h` - TBB 流图执行引擎

**核心类**:
- `ExecutionGraph`: TBB 图容器，提供 `Reset()` 方法重置图状态
- `Node`: 所有节点的抽象基类
  - `build(ExecutionGraph&)`: 纯虚函数，创建 TBB 节点
  - `connect()`: 纯虚函数，建立节点间连接（延迟连接）
  - `Activate()`: 虚函数，激活节点（主要用于 SourceNode）
  - `upstreams_`: 存储上游节点引用的向量

**三种节点类型**:

1. **SourceNode** (`tbb::flow::input_node<Frame>`)
   - 数据源节点，生成 `Frame` 对象
   - 通过 `flow_control` 控制停止条件
   - 提供 `activate()` 方法启动数据流

2. **ProcessNode** (`tbb::flow::function_node<Frame, Frame>`)
   - 数据处理节点，串行执行策略
   - 支持从 `SourceNode` 或 `ProcessNode` 接收输入
   - 纯虚函数 `process(Frame)` 由子类实现具体逻辑

3. **SinkNode** (`tbb::flow::function_node<Frame, continue_msg>`)
   - 终端节点，消费数据流
   - 返回 `continue_msg` 保持流程继续
   - 纯虚函数 `consume(Frame)` 由子类实现具体逻辑

**延迟连接机制**:
```cpp
// Phase 1: Build (创建所有节点)
void build(ExecutionGraph& g) override {
    tbb_node = std::make_unique<tbb::flow::function_node<Frame, Frame>>(...);
}

// Phase 2: Connect (连接节点间的边)
void connect() override {
    for (auto* upstream : upstreams_) {
        tbb::flow::make_edge(*(upstream->tbb_node), *(this->tbb_node));
    }
}
```

#### `ops_opencv.h` - OpenCV 算子实现

**算子列表**:

1. **CameraSource** (`SourceNode`)
   - 支持 Mock 模式（`device_id=-1`）
   - Mock 模式生成红/蓝/白循环测试图案
   - 支持帧数限制（`set_limit(n)`）
   - 自动帧时间戳（纳秒级精度）

2. **CannyFilter** (`ProcessNode`)
   - 边缘检测算子
   - 自动转换到灰度图
   - 阈值：100/200

3. **NullSink** (`SinkNode`)
   - 空消费者，用于测试和分支输出
   - 仅记录日志（trace 级别）

4. **VideoWriterSink** (`SinkNode`)
   - 视频输出到文件
   - 编码格式：MJPEG
   - 帧率：30 FPS
   - 自动检测通道数（彩色/灰度）

5. **PyFuncNode** (`ProcessNode`)
   - **混合执行核心**
   - 在 C++ 流程中调用 Python 回调
   - 自动 GIL 管理（`pybind11::gil_scoped_acquire`）
   - 异常处理：失败时返回原帧
   - 使用 `spdlog::trace` 记录执行

#### `memory/frame.h` - 内存管理

**FrameBuffer 结构**:
```cpp
struct FrameBuffer {
    cv::Mat mat;              // OpenCV 内存后端
    void* data;               // 通用数据指针
    int width, height;        // 尺寸
    size_t stride;            // 行字节跨度
    DeviceType device;        // 设备类型 (CPU/CUDA/Vulkan)
    uint64_t timestamp;       // 纳秒级时间戳
};
```

**设计特点**:
- 使用 `std::shared_ptr<FrameBuffer>` 作为 `Frame` 类型
- 支持图分叉（多个消费者共享同一帧）
- OpenCV `aligned_malloc` 确保内存对齐
- 设备感知枚举（Phase 3 预留 CUDA/Vulkan）

### 3.2 Python 绑定层 (`src/bindings/`)

#### `bindings.cpp` - pybind11 接口

**关键绑定**:

1. **Frame** (FrameBuffer)
   ```python
   class Frame:
       width: int          # 只读
       height: int         # 只读
       # Buffer Protocol: 支持 np.array(frame, copy=False)
   ```

2. **ExecutionGraph**
   ```python
   class ExecutionGraph:
       def reset() -> None
   ```

3. **Executor**
   ```python
   class Executor:
       def run(graph: ExecutionGraph) -> None
       # 调用期间释放 GIL (py::gil_scoped_release)
   ```

4. **Node 基类**
   ```python
   class Node:
       def connect() -> None
       def activate() -> None
   ```

5. **具体算子**
   - `CameraSource(device_id: int)`: 摄像头源
   - `CannyFilter()`: Canny 边缘检测
   - `NullSink()`: 空消费
   - `VideoWriterSink(filename: str)`: 视频写入
   - `PyFuncNode(py_callable)`: Python 函数节点

### 3.3 Python API (`python/easywork/`)

#### `__init__.py` - 高层接口

**核心组件**:

1. **Symbol**
   ```python
   class Symbol:
       producer_node: Node  # 生产此符号的节点
   ```

2. **NodeWrapper** (所有节点基类)
   ```python
   class NodeWrapper:
       _cpp_node: Any       # C++ 节点实例
       built: bool          # 构建状态
       raw: Any             # 访问底层 C++ 节点
   ```

3. **具体节点**
   - `Camera(device_id=-1, limit=-1)`: 摄像头
     - `read() -> Symbol`: 返回输出符号

   - `Canny()`: 边缘检测
     - `process(input_symbol) -> Symbol`

   - `NullSink()`: 空消费
     - `consume(input_symbol) -> None`

   - `VideoWriter(filename)`: 视频输出
     - `write(input_symbol) -> None`

   - `PyFunc(py_callable)`: Python 函数包装
     - `call(input_symbol) -> Symbol`

4. **Pipeline** (PyTorch 风格 API)
   ```python
   class Pipeline:
       def __init__(self):
           # 自动初始化图和执行器
           self._graph = ExecutionGraph()
           self._executor = Executor()
           self._nodes = []  # 节点列表

       def __setattr__(self, name, value):
           # 魔术方法：自动注册 NodeWrapper
           if isinstance(value, NodeWrapper):
               self._nodes.append(value)

       def construct(self):
           # 用户重写：定义拓扑结构
           raise NotImplementedError

       def run(self):
           # 三阶段执行
           # 1. Trace: 执行 construct() 定义连接
           # 2. Build: 创建所有 C++ 节点
           # 3. Connect: 建立边连接
           # 4. Execute: 运行图
   ```

**使用示例**:
```python
class MyPipeline(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.cam = ew.Camera(device_id=-1, limit=15)
        self.proc = ew.PyFunc(self.process_frame)
        self.writer = ew.VideoWriter("output.avi")

    def process_frame(self, frame):
        img = np.array(frame, copy=False)  # 零拷贝
        cv2.circle(img, (320, 240), 50, (0, 255, 0), -1)
        return frame

    def construct(self):
        x = self.cam.read()
        y = self.proc(x)
        self.writer.write(y)

app = MyPipeline()
app.run()
```

## 4. 构建系统 (`CMakeLists.txt`)

### 依赖检测策略

1. **OpenCV**: 必须 (`find_package(OpenCV REQUIRED)`)

2. **TBB**: 优先 oneTBB 2021+
   ```cmake
   find_package(TBB 2021 QUIET CONFIG)  # 优先
   find_package(TBB QUIET)               # 降级
   pkg_check_modules(TBB REQUIRED tbb>=2021)  # 最后
   ```

3. **spdlog**: 强制源码构建
   ```cmake
   FetchContent_Declare(spdlog
       GIT_REPOSITORY https://github.com/gabime/spdlog.git
       GIT_TAG v1.12.0
   )
   ```

4. **pybind11**: 灵活获取
   ```cmake
   # 优先 pip 安装版本
   execute_process(COMMAND python -c "import pybind11; ...")
   # 降级到 FetchContent
   FetchContent_Declare(pybind11 GIT_TAG v2.11.1)
   ```

### 输出配置

- 产物：`python/easywork/easywork_core.so`
- Python 版本：3.8+
- 链接库：OpenCV, TBB, spdlog

## 5. 开发路线 (Development Roadmap)

### ✅ Phase 1: 核心骨架 (已完成)

**目标**: 验证 "Trace → Compile → Run" 通路

**成果**:
- TBB 流图基础封装
- Python 绑定验证
- 基本数据流测试

### ✅ Phase 2: 工业级重构 (已完成)

**目标**: 生产可用的执行引擎

**成果**:
1. **架构优化**
   - 延迟连接模式（Deferred Connection）
   - 零拷贝 Buffer Protocol
   - 混合执行（C++ 调用 Python）

2. **算子库**
   - CameraSource (Mock/Real)
   - CannyFilter
   - NullSink
   - VideoWriterSink
   - PyFuncNode

3. **API 设计**
   - PyTorch 风格 `Pipeline` 类
   - Symbol 符号系统
   - 自动节点注册

4. **工程化**
   - CMake 构建系统
   - ABI 兼容性处理（spdlog 源码构建）
   - GIL 管理（`gil_scoped_release/acquire`）

### 🚧 Phase 3: 控制流 (进行中)

**目标**: 支持动态控制流

**计划任务**:
1. **分支控制**
   - 利用 TBB `indexer_node` 实现路由
   - `ew.If(cond, true_branch, false_branch)`
   - Python 上下文管理器语法

2. **循环控制**
   - `ew.Loop(body, max_iterations)`
   - `ew.While(cond, body)`
   - 循环变量管理

3. **高级特性**
   - 条件谓词节点
   - 动态图拓扑
   - AST 解析（Numba/Triton 风格）

### 📋 Phase 4: 性能优化 (计划)

- 内存池管理
- GPU 算子（CUDA/Vulkan）
- 异步 I/O
- 流水线并行优化

### 📋 Phase 5: 生态扩展 (远期)

- 预训练模型集成
- ONNX Runtime 支持
- ROS/ROS2 接口
- 嵌入式部署工具

## 6. 技术亮点

### 6.1 零拷贝数据传输

**实现机制**:
```python
# Python 端
img = np.array(frame, copy=False)  # 直接访问 C++ 内存
```

**底层实现** (bindings.cpp):
```cpp
.def_buffer([](FrameBuffer &f) -> py::buffer_info {
    return py::buffer_info(
        f.data,                    // 指向 C++ 内存
        sizeof(unsigned char),
        py::format_descriptor<unsigned char>::format(),
        3,                         // 3D: (H, W, C)
        { f.height, f.width, 3 },
        { f.stride, 3, sizeof(unsigned char) }
    );
})
```

### 6.2 混合执行模型

**特点**:
- C++ 流程中嵌入 Python 逻辑
- 自动 GIL 管理（C++ 执行时释放，Python 调用时获取）
- 异常隔离（Python 错误不崩溃 C++）

**实现** (ops_opencv.h:122-141):
```cpp
class PyFuncNode : public ProcessNode {
    Frame process(Frame input) override {
        pybind11::gil_scoped_acquire acquire;  // 获取 GIL
        try {
            pybind11::object result = func_(pybind11::cast(input));
            return result.cast<Frame>();
        } catch (const std::exception& e) {
            spdlog::error("Python execution failed: {}", e.what());
            return input;  // 失败时返回原帧
        }
    }
};
```

### 6.3 延迟连接模式

**优势**:
- 支持任意拓扑定义顺序
- 节点创建和连接分离
- 便于动态图构建

**流程**:
```
Trace (定义符号) → Build (创建节点) → Connect (连接边) → Execute (运行)
```

## 7. 测试覆盖

| 测试文件 | 功能覆盖 |
|---------|---------|
| `test_phase1.py` | 基础数据流验证 |
| `test_phase2_cam.py` | 真实摄像头采集 |
| `test_phase2_class.py` | Pipeline 类 API |
| `test_phase2_hybrid.py` | Python 回调混合执行 |
