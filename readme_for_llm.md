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
        *   *注意*: 目前部分原型代码可能使用 `snake_case`，新代码请使用 `PascalCase`，旧代码在重构时逐步修正。
    *   **命名空间 (Namespaces)**: `snake_case` (例如: `easywork`)
*   **注释**: 使用英文编写，清晰描述 *Why* 而非 *What*。
*   **头文件**: 使用 `#pragma once`。
*   **错误处理**: 优先使用返回值或 `std::optional`，异常处理需谨慎使用（遵循 Google 规范）。

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

## 3. 项目结构说明

```text
easywork/
├── build/              # CMake 构建目录
├── extern/             # 第三方依赖 (TBB 等)
├── python/             # Python 包源码
│   └── easywork/       # Python 包主目录
├── src/                # C++ 源码
│   ├── bindings/       # pybind11 绑定代码
│   └── runtime/        # 核心运行时 (TBB graph, OpenCV ops)
│       └── memory/     # 内存管理
├── tests/              # 测试脚本 (Python)
├── CMakeLists.txt      # 主 CMake 配置文件
└── readme_for_llm.md   # 本文档
```

## 4. 工具链与开发流程

*   **构建系统**: CMake (>= 3.10)
*   **C++ 标准**: C++17
*   **Python 版本**: Python 3.8+
*   **关键库**:
    *   Intel TBB (Threading Building Blocks) - 并行流图执行
    *   OpenCV - 图像处理
    *   pybind11 - C++/Python 互操作
    *   spdlog - 日志记录
*   **测试**: 使用 `pytest` 运行 `tests/` 目录下的测试脚本。
*   **开发循环**:
    1.  修改代码 (C++ 或 Python)。
    2.  如果修改了 C++，在 `build/` 目录下运行 `make -j` 重新编译扩展。
    3.  运行测试 `python3 -m pytest tests/` 或特定测试脚本。

## 5. 特别说明

*   **EasyWork 愿景**: 实现类似 Numba/Triton 的 AST 解析，支持原生 Python `if/else` 语法在图编译器中的转换。
*   **原型阶段**: 目前处于原型阶段，重点是验证 TBB 流图与 Python 的绑定机制。
