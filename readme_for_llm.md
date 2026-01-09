# LLM 开发指南 & 项目规范

本文档旨在为参与 EasyWork 项目的大语言模型（LLM）及开发者提供统一的开发规范、代码风格及项目结构说明。请严格遵守以下标准。

## 1. 核心原则

*   **语言**: 与人类用户沟通以及文档都必须使用 **中文**，代码注释使用 **英文**。
*   **风格**: 严格遵循 Google 代码规范。
*   **一致性**: 在修改现有文件时，优先保持与上下文一致，但新模块应严格符合本规范。

## 2. 代码规范

### 2.1 C++ 代码规范

遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)。

*   **格式化**: 使用项目根目录下的 `.clang-format` 配置文件（基于 Google Style）。
*   **命名约定**:
    *   **类型名 (Classes, Structs, Type aliases, Enums)**: `PascalCase` (例如: `ExecutionGraph`, `FrameBuffer`)
    *   **变量名 (Variables)**: `snake_case` (例如: `frame_width`, `input_node`)
    *   **类成员变量 (Class Data Members)**: `snake_case_` (末尾加下划线，例如: `upstreams_`, `tbb_node_`)
    *   **常量 (Constant Names)**: `kPascalCase` (例如: `kMaxBufferSize`) 或 `ALL_CAPS` (宏)
    *   **函数名 (Function Names)**: `PascalCase` (例如: `Build()`, `Connect()`, `SetInput()`)
        *   *注意*: 目前部分原型代码使用 `snake_case`（如 `build`, `connect`），新代码请使用 `PascalCase`，旧代码在重构时逐步修正。
    *   **命名空间 (Namespaces)**: `snake_case` (例如: `easywork`)
*   **注释**: 使用英文编写，清晰描述 *Why* 而非 *What*。
*   **头文件**: 使用 `#pragma once`。
*   **错误处理**: 优先使用返回值或 `std::optional`，异常处理需谨慎使用（遵循 Google 规范）。

**代码示例**:
```cpp
// Good: 清晰的命名和注释
class ProcessNode : public Node {
public:
    void Build(ExecutionGraph& g) override {
        // Create TBB function node with serial policy
        tbb_node_ = std::make_unique<tbb::flow::function_node<Frame, Frame>>(
            g.tbb_graph,
            tbb::flow::serial,
            [this](Frame f) -> Frame {
                if (!f) return nullptr;  // Skip null frames
                return this->Process(f);
            }
        );
    }

    virtual Frame Process(Frame input) = 0;

private:
    std::unique_ptr<tbb::flow::function_node<Frame, Frame>> tbb_node_;
};
```

### 2.2 Python 代码规范

遵循 [Google Python Style Guide](https://google.github.io/styleguide/pyguide.html)。

*   **格式化**: 符合 PEP 8 标准。
*   **命名约定**:
    *   **模块名 (Modules)**: `snake_case` (例如: `easywork_core`)
    *   **类名 (Classes)**: `PascalCase` (例如: `ImageProcessor`)
    *   **函数/方法名 (Functions/Methods)**: `snake_case` (例如: `process_image()`, `add_node()`)
    *   **变量名 (Variables)**: `snake_case`
    *   **常量 (Constants)**: `ALL_CAPS` (例如: `DEFAULT_TIMEOUT`)
*   **类型提示 (Type Hints)**: 强烈建议在函数签名中使用 Python 类型提示 (Type Hints)。
*   **Docstrings**: 使用 Google 风格的 Docstrings。

**代码示例**:
```python
class Pipeline:
    """PyTorch-style pipeline for defining computation graphs."""

    def __init__(self) -> None:
        """Initialize the pipeline with empty node list."""
        self._graph = _core.ExecutionGraph()
        self._nodes: list[NodeWrapper] = []

    def construct(self) -> None:
        """Define the computation graph topology.

        Raises:
            NotImplementedError: Subclass must implement this method.
        """
        raise NotImplementedError("You must implement construct() method")
```

## 3. 项目结构说明

```
prototype/
├── build/              # CMake 构建目录（自动生成）
├── build_sys/          # 备用构建目录
├── python/             # Python 包源码
│   └── easywork/       # Python 包主目录
│       ├── __init__.py # Python API 和包装器
│       └── easywork_core.so  # C++ 扩展（编译后）
├── src/                # C++ 源码
│   ├── bindings/       # pybind11 绑定代码
│   │   └── bindings.cpp
│   └── runtime/        # 核心运行时
│       ├── core_tbb.h      # TBB 流图执行引擎
│       ├── ops_opencv.h    # OpenCV 算子实现
│       └── memory/         # 内存管理
│           └── frame.h     # FrameBuffer 结构
├── tests/              # 测试脚本 (Python)
│   ├── test_phase1.py
│   ├── test_phase2_cam.py
│   ├── test_phase2_class.py
│   └── test_phase2_hybrid.py
├── CMakeLists.txt      # 主 CMake 配置文件
├── design_doc.md       # 技术设计文档
└── readme_for_llm.md   # 本文档
```

## 4. 工具链与开发流程

### 4.1 构建系统

*   **构建工具**: CMake (>= 3.15)
*   **C++ 标准**: C++17
*   **Python 版本**: Python 3.8+

### 4.2 关键依赖

| 依赖 | 版本要求 | 检测方式 |
|------|----------|----------|
| **OpenCV** | 任意 | `find_package(OpenCV REQUIRED)` |
| **Intel TBB** | 2021+ | 优先 oneTBB，降级到 pkg-config |
| **spdlog** | v1.12.0 | 强制 FetchContent 源码构建 |
| **pybind11** | v2.11.1+ | 优先 pip 安装，降级到 FetchContent |

**依赖说明**:
- **spdlog**: 强制源码构建以确保 ABI 兼容性（避免系统版本冲突）
- **pybind11**: 优先检测 pip 安装版本，避免重复下载
- **TBB**: 支持 oneTBB 2021+ 和传统 TBB

### 4.3 构建步骤

```bash
# 1. 配置 CMake
mkdir build && cd build
cmake ..

# 2. 编译
make -j$(nproc)

# 3. 产物位置
# python/easywork/easywork_core.so
```

### 4.4 测试流程

```bash
# 运行所有测试
python3 -m pytest tests/

# 运行单个测试
python3 tests/test_phase2_class.py

# 测试说明
# test_phase1.py         - 基础数据流验证
# test_phase2_cam.py     - 真实摄像头采集
# test_phase2_class.py   - Pipeline 类 API
# test_phase2_hybrid.py  - Python 回调混合执行
```

### 4.5 开发循环

1. **修改代码** (C++ 或 Python)
2. **如果修改了 C++**:
   - 在 `build/` 目录运行 `make -j` 重新编译
   - 确保没有编译错误和警告
3. **运行测试**:
   - 运行相关测试脚本验证功能
   - 检查日志输出（spdlog trace 级别）
4. **提交前检查**:
   - 代码符合规范
   - 注释清晰（英文）
   - 测试通过

## 5. 核心概念

### 5.1 延迟连接模式 (Deferred Connection)

EasyWork 使用两阶段构建模式：

**Phase 1: Build** - 创建所有节点
```cpp
void Build(ExecutionGraph& g) override {
    tbb_node_ = std::make_unique<...>(g.tbb_graph, ...);
}
```

**Phase 2: Connect** - 建立节点间连接
```cpp
void Connect() override {
    for (auto* upstream : upstreams_) {
        tbb::flow::make_edge(*(upstream->tbb_node_), *(this->tbb_node_));
    }
}
```

**优势**:
- 支持任意拓扑定义顺序
- 节点创建和连接分离
- 便于动态图构建

### 5.2 零拷贝数据传输

通过 Python Buffer Protocol 实现：

```python
# Python 端：直接访问 C++ 内存
img = np.array(frame, copy=False)
```

**实现机制** (bindings.cpp):
```cpp
.def_buffer([](FrameBuffer &f) -> py::buffer_info {
    return py::buffer_info(
        f.data,                                // 指向 C++ 内存
        sizeof(unsigned char),
        py::format_descriptor<unsigned char>::format(),
        3,                                     // (H, W, C)
        { f.height, f.width, 3 },
        { f.stride, 3, sizeof(unsigned char) }
    );
})
```

### 5.3 GIL 管理

**Executor.run()**: 释放 GIL 允许并行执行
```cpp
.def("run", &Executor::run, py::call_guard<py::gil_scoped_release>())
```

**PyFuncNode**: 获取 GIL 调用 Python 代码
```cpp
Frame process(Frame input) override {
    pybind11::gil_scoped_acquire acquire;
    // 调用 Python 函数
}
```

### 5.4 节点类型

| 类型 | TBB 节点 | 用途 |
|------|----------|------|
| **SourceNode** | `input_node<Frame>` | 数据源（摄像头、文件） |
| **ProcessNode** | `function_node<Frame, Frame>` | 数据处理（滤波、推理） |
| **SinkNode** | `function_node<Frame, continue_msg>` | 终端消费（显示、保存） |

## 6. 代码审查要点

### 6.1 C++ 代码

- [ ] 使用 `PascalCase` 命名类和函数
- [ ] 成员变量使用 `snake_case_` 并添加末尾下划线
- [ ] 使用英文注释，描述 "Why" 而非 "What"
- [ ] 头文件使用 `#pragma once`
- [ ] 资源管理使用 RAII（智能指针）
- [ ] GIL 管理正确（`gil_scoped_release/acquire`）

### 6.2 Python 代码

- [ ] 符合 PEP 8 规范
- [ ] 类名使用 `PascalCase`
- [ ] 函数名使用 `snake_case`
- [ ] 添加类型提示
- [ ] 使用 Google 风格 Docstrings
- [ ] 零拷贝操作使用 `copy=False`

### 6.3 架构设计

- [ ] 新节点继承正确的基类（SourceNode/ProcessNode/SinkNode）
- [ ] 实现 `Build()` 和 `Connect()` 方法
- [ ] 使用 `add_upstream()` 存储上游节点
- [ ] 异常处理完善（特别是 Python 互操作）
- [ ] 日志使用合适的级别（trace/info/error）

## 7. 常见问题

### Q1: 为什么 spdlog 要源码构建？

**A**: 确保 ABI 兼容性。系统预装的 spdlog 可能使用不同的编译器或标志，导致链接错误。

### Q2: Mock 模式如何使用？

**A**: `Camera(device_id=-1)` 启用 Mock 模式，生成红/蓝/白循环测试图案。

```python
cam = ew.Camera(device_id=-1, limit=10)  # 生成 10 帧测试数据
```

### Q3: 如何添加新算子？

**A**: 三个步骤：

1. **C++ 端** (ops_opencv.h):
```cpp
class MyFilter : public ProcessNode {
    Frame Process(Frame input) override {
        // 实现逻辑
    }
};
```

2. **绑定端** (bindings.cpp):
```cpp
py::class_<MyFilter, Node, std::shared_ptr<MyFilter>>(m, "MyFilter")
    .def(py::init<>())
    .def("build", &MyFilter::Build)
    .def("set_input", &MyFilter::SetInput);
```

3. **Python 端** (__init__.py):
```python
class MyFilter(NodeWrapper):
    def __init__(self):
        super().__init__(_core.MyFilter())

    def process(self, input_symbol):
        self._cpp_node.set_input(input_symbol.producer_node)
        return Symbol(self._cpp_node)
```

### Q4: 如何调试数据流问题？

**A**: 启用 spdlog trace 日志：

```cpp
spdlog::set_level(spdlog::level::trace);
```

检查点：
- 节点是否正确添加到 `Pipeline._nodes`
- `Connect()` 是否被调用
- 上游节点是否正确存储在 `upstreams_`

## 8. 项目愿景

**EasyWork** 的长期目标是实现类似 **Numba/Triton** 的 AST 解析，支持：

1. **原生 Python 控制流**
   ```python
   if frame.mean() > 128:
       frame = process_bright(frame)
   else:
       frame = process_dark(frame)
   ```

2. **自动图编译**
   - 解析 Python AST
   - 提取数据流图
   - 生成 TBB 执行代码

3. **当前状态**: Phase 2 完成，Phase 3（控制流）开发中

## 9. 参考资源

- [Intel TBB 官方文档](https://oneapi-src.github.io/oneTBB/)
- [pybind11 文档](https://pybind11.readthedocs.io/)
- [OpenCV 文档](https://docs.opencv.org/)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [Google Python Style Guide](https://google.github.io/styleguide/pyguide.html)
