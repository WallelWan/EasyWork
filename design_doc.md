# EasyWork: 高性能嵌入式 AI 图编译器 - 设计文档

## 1. 项目概述 (Project Overview)
**EasyWork** 是一个专为嵌入式 AI 应用（如无人机、机器人、自动驾驶）设计的领域特定系统 (DSL)。
EasyWork 采用 **“追踪-编译-执行”** 架构，前端 Python 定义，后端 C++ TBB 执行。

## 2. 系统架构 (System Architecture)

### 2.1 核心技术栈 (Tech Stack)
*   **调度引擎**: **Intel OneTBB 2021** (与重新编译的 OpenCV 兼容)
    *   利用 `tbb::flow::graph` 实现工业级调度。
*   **内存管理**: **自研 FrameBuffer (Hardware Aware)**
    *   支持零拷贝 (Zero-Copy) 到 NumPy。
*   **图像算法**: **OpenCV**
    *   仅作为“算子实现库”使用。
*   **绑定层**: **pybind11**
*   **日志**: **spdlog**

## 3. 核心模块 (Core Modules)

### 3.1 C++ 运行时 (`src/runtime/`)
*   **`core_tbb.h`**: 封装 TBB Graph，实现延迟连接 (Deferred Connection)。
*   **`ops_opencv.h`**: 
    *   `CameraSource`: 支持 Mock 模式。
    *   `PyFuncNode`: 支持 Python 回调 (混合执行)。
*   **`memory/frame.h`**: 核心数据结构。

## 4. 开发路线 (Development Roadmap)

### Phase 1: 核心骨架 (已完成)
*   验证 "Trace -> Compile -> Run" 通路。

### Phase 2: 工业级重构 (已完成)
*   **成果**:
    *   集成了 TBB + OpenCV。
    *   解决了 ABI 版本冲突 (Header Downgrade)。
    *   实现了类 PyTorch 的 `ew.Pipeline` API。
    *   实现了混合执行 (Python Operator)。

### Phase 3: 控制流 (下一步)
*   **目标**: 支持 `ew.If` / `ew.Loop`。
*   **任务**:
    1.  利用 TBB `indexer_node` 实现分支路由。
    2.  Python 端实现上下文管理器语法糖。
