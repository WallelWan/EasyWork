# EasyWork 项目文档（统一版）

本文档整合并替代项目内分散的说明文件，覆盖架构、类型系统、API 使用、构建与测试规范。内容以当前实现为准，删除过时或不再适用的描述。

## 1. 项目概述

EasyWork 是基于 Taskflow 静态调度的计算图执行框架，提供：

- C++ 运行时与节点系统
- Python API（构建数据流图）
- 类型系统与 `Value` 类型擦除容器
- 扩展性：新增节点或新类型可通过 C++ 模块注册

## 2. 架构与执行模型

### 2.1 两阶段构建（Deferred Connection）

1. **Build**：创建 Taskflow 任务并构建执行图内部结构  
2. **Connect**：根据上游关系进行节点连接  

该模式允许任意拓扑定义顺序，并简化连接逻辑。

### 2.2 数据流与零拷贝

Frame 通过 `std::shared_ptr<FrameBuffer>` 传递；Python 侧通过 buffer 协议访问 C++ 内存，避免复制。

## 3. 类型系统

### 3.1 TypeInfo / NodeTypeInfo

- `TypeInfo` 保存 RTTI 与类型哈希，支持比较  
- `NodeTypeInfo` 记录节点输入/输出类型签名，供 `Pipeline.validate()` 使用  

### 3.2 Value 类型擦除与 SBO

`Value` 用于跨节点传递任意类型，并使用 small-buffer optimization（SBO）减少堆分配。对小对象进行安全的拷贝/移动/析构处理，避免生命周期问题。

### 3.3 Tuple 与自动索引

Python 端支持 `a, b = node.read()` 形式的 tuple 解包，依赖 C++ 端的 tuple 类型注册：

```cpp
RegisterTupleType<std::tuple<int, std::string>>();
```

注册后 Python 会通过 `create_tuple_get_node()` 自动插入 `TupleGetNode` 进行元素索引。

## 4. Python API

### 4.1 Symbol

`Symbol` 表示数据流连接结果，可用于：

- `node(symbol)`：连接单输入
- `node(symbol1, symbol2, ...)`：连接多输入
- `a, b = symbol`：tuple 解包（需注册）

### 4.2 Pipeline

- `construct()`：用户定义拓扑  
- `validate()`：构建并执行类型检查  
- `run()`：执行图（会自动调用 `validate()`）  

类型检查基于节点输入/输出类型签名，连接数量与类型不匹配会抛出 `TypeError`。

## 5. 运行时节点分类

| 节点类型 | 作用 |
|---|---|
| Source / InputNode | 数据源（如 Camera） |
| FunctionNode | 处理节点（输入/输出） |
| SinkNode | 消费节点 |

推荐实现 `TypedInputNode`、`TypedFunctionNode` 或 `TypedMultiInputFunctionNode` 以获得完整类型信息。

## 6. 扩展节点

### 6.1 添加 C++ 节点

1. 在 `src/runtime/module/` 添加节点实现  
2. 使用 `EW_REGISTER_NODE*` 宏注册  
3. 在 `src/runtime/modules.h` 引入头文件  

### 6.2 Python 侧访问

注册后的节点可通过 `ew.module.YourNode()` 获取。

## 7. 构建与测试

### 7.1 构建

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 7.2 测试

```bash
python3 -m pytest tests/
```

若只测试类型系统：

```bash
python3 tests/test_complete_type_system.py
```

## 8. 贡献规范（代码风格）

### 8.1 通用约定

- 与用户沟通与文档使用中文  
- 代码注释使用英文  
- 变更尽量保持局部一致性  

### 8.2 C++ 风格

- Google C++ Style  
- 类名 `PascalCase`  
- 成员变量 `snake_case_`  
- 函数名 `PascalCase`  
- 头文件使用 `#pragma once`  

### 8.3 Python 风格

- PEP 8 / Google Python Style  
- 类名 `PascalCase`  
- 函数与变量 `snake_case`  
- 建议添加类型注解  
