# EasyWork

EasyWork 是一个 C++20 + Python 的异构计算图运行时。它基于 Taskflow 进行静态调度，通过 pybind11 暴露节点到 Python，并提供“即时执行 + 构图执行”的混合 API。

## 核心特性

- **Taskflow 静态调度**：构图一次，重复执行，运行时开销低。
- **异构方法**：单个节点可暴露多个不同签名的方法。
- **连接时类型检查**：在连接阶段完成类型检查与转换准备。
- **Python 优先体验**：pipeline 外立即执行，pipeline 内自动构图。
- **零拷贝数据支持**：`Packet` 基于 `std::any` 共享数据；`FrameBuffer` 支持 Python Buffer Protocol。
- **AST 控制流（严格模式）**：`construct()` 中支持原生 `if/else`，并做构图期检查。
- **严格 GraphSpec v1 合同**：必须提供 `schema_version`；边只允许方法名；旧字段 `method_id` 与 `method_config.sync` 会被拒绝。
- **统一调度核心**：`BaseNode` 与 `PyNode` 共用同一套分发计划/执行主线。
- **运行时核心已模块化**：`core.h` 作为聚合入口，内部拆分为 `ids.h`、`execution_graph.h`、`node.h`、`base_node.h`、`executor.h` 等。

## 快速开始

### 构建

要求：

- C++20 编译器（GCC 10+, Clang 12+, MSVC 2019+）
- CMake 3.15+
- Python 3.8+（仅当 `EASYWORK_BUILD_PYTHON=ON`）
- OpenCV（仅当 `EASYWORK_WITH_OPENCV=ON`）

```bash
# Runtime-only（不依赖 Python/OpenCV）
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt

# Full build
cmake -S . -B build_full -DEASYWORK_BUILD_PYTHON=ON -DEASYWORK_WITH_OPENCV=ON
cmake --build build_full
```

启用 Python 构建时，扩展 `easywork_core` 会输出到 `python/easywork`。
构建矩阵和交叉编译请见 `doc/build.md` 与 `doc/cross_build.md`。

### Python 用法

```python
import easywork as ew

class MyPipeline(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.src = ew.module.NumberSource(start=0, max=3)
        self.mul = ew.module.MultiplyBy(factor=2)
        self.to_text = ew.module.IntToText()
        self.prefix = ew.module.PrefixText(prefix="[Value] ")

    def construct(self):
        value = self.src.read()
        text = self.to_text(self.mul(value))
        self.prefix(text)

pipeline = MyPipeline()
pipeline.validate()  # 可选但推荐
pipeline.open()      # run() 前必须调用
pipeline.run()
pipeline.close()
```

### 图导出/导入（Python -> JSON -> C++）

导出仅包含 C++ 节点的 Pipeline 到 JSON：

```python
pipeline = MyPipeline()
pipeline.validate()
pipeline.export("graph.json")
```

在 C++ 中读取 JSON 并运行：

```cpp
#include "runtime/core/graph_build.h"

auto graph = easywork::GraphBuild::FromJsonFile("graph.json");
graph->Run();
```

说明：

- 图中包含 Python 节点或内部节点（如 tuple 解包节点）会直接终止导出。
- 构造参数只支持基本 JSON 类型（bool/int/float/string）。
- GraphSpec 不包含 Node open/close 参数。
- 运行时加载器要求 `schema_version == 1`。
- 历史字段 `from.method_id` / `to.method_id` 不再接受。
- `method_config.sync` 不再接受。
- 旧图可通过 `scripts/migrate_graph_ir_v1.py` 迁移为 v1。

### Runtime-only 可执行文件

提供无需 Python 的 runtime 可执行程序：

```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
./build_rt/easywork_runtime_example
./build_rt/easywork-run --graph graph.json
./build_rt/easywork-run --graph graph.json --log-level info --log-format json --log-file /tmp/easywork.log
```

运行 C++ 测试：
```bash
ctest --test-dir build_rt
```

### Python 节点（自动注册）

Python 端可直接定义节点类，继承 `ew.PythonNode` 即可自动注册到 C++ 核心：

```python
import easywork as ew

class PyScale(ew.PythonNode):
    __ew_methods__ = ("forward", "reset")

    def __init__(self, factor=2):
        self.factor = factor

    def forward(self, x, scale=1):
        return x * self.factor * scale

    def reset(self):
        self.factor = 1
```

调用方式：

```python
node = ew.module.PyScale(factor=3)
node(10)           # forward
node.forward(10)
node.reset()
```

### kwargs 与默认值（Python 节点）

- **即时模式**支持 `kwargs`。
- **构图模式**（Pipeline 内）也支持 `kwargs`，但仅对 Python 节点有效。
- 构图阶段会根据参数名将 `kwargs` 映射为固定输入顺序。
- Python 方法的默认值在构图阶段可用（未提供参数时自动补齐）。默认值会自动转换为内部常量节点 `_PyConst`。
- `*args/**kwargs` 的方法在构图阶段不支持 `kwargs`。

### 即时执行（非 Pipeline）

```python
multiplier = ew.module.MultiplyBy(factor=5)
result = multiplier(10)  # 立即执行，返回 int
```

## 执行模型

- **即时模式**：在 Pipeline 之外调用节点会直接执行并返回 Python 值。
- **构图模式**：在 `Pipeline.construct()` 或 `with pipeline:` 中调用节点会返回 `Symbol`，用于构建连接关系。
- **运行流程**：`validate()` 负责构图和类型检查；`run()` 会构建 Taskflow 任务、连接依赖并执行；重复 `run()` 会自动重置图。
- **Open/Close 约束**：`run()` 前必须 `open()`；`Node.open()`/`Node.close()` 只支持位置参数，并且会严格校验参数数量。
- **Python 节点参数规则**：Pipeline 内允许 Python 节点使用 `kwargs` 与默认值；C++ 节点仍仅支持位置参数。
- **错误策略 API**：`Pipeline.set_error_policy(...)` 仅接受 `_core.ErrorPolicy` 枚举值。

## 类型系统与转换

- **TypeInfo**：基于 `std::type_info` 的轻量描述，含 demangle 名称。
- **Packet**：`std::any` 载荷 + 时间戳；空包表示 void 返回。
- **转换机制**：`TypeConverterRegistry` 提供连接时转换，内置算术类型提升与 Python 转换（pybind11）。
- **Tuple 支持**：返回类型为 `std::tuple` 的方法会自动注册，可直接在 Python 端拆包。
- **Mux 安全性**：通过 `MuxSymbol` 连接的输入必须类型一致，且同一输入索引不能混用 mux 与直接连接。

## 校验与构图说明

- **IfNode 条件**：`construct()` 中的 `if` 条件必须是 `bool` 或 `int`（含 int64），类型不符会在构图期报错。
- **Mux 控制类型**：`MuxSymbol` 的控制输入必须输出 `bool`/`int`，类型不符会在构图期报错。
- **类型校验**：若扩展提供转换器，则允许可转换类型；否则严格要求类型一致。
- **NumberSource 步长**：`step` 必须为非零整数，非法值会在创建节点时抛错。

## C++ 节点开发

使用 `BaseNode<T>` + `EW_ENABLE_METHODS` 定义方法，再用 `EW_REGISTER_NODE` 注册到 Python。

```cpp
#include "runtime/core/core.h"
#include "runtime/registry/node_registry.h"
#include "runtime/registry/macros.h"

class Scaler : public easywork::BaseNode<Scaler> {
public:
    explicit Scaler(int factor) : factor_(factor) {}

    int forward(int x) { return x * factor_; }
    void reset() { factor_ = 1; }

    EW_ENABLE_METHODS(forward, reset)

private:
    int factor_{1};
};

EW_REGISTER_NODE(Scaler, "Scaler", easywork::Arg("factor", 1))
```

说明：

- 方法签名会被反射并用于连接时类型检查。
- `void` 返回的方法会产生空包，不能连接到需要数据的下游。
- 可通过 `set_method_order`、`set_method_queue_size` 配置调度与缓冲。

## 项目结构

```
src/
  runtime/
    core/            # ExecutionGraph, Node, dispatch 逻辑
    types/           # TypeInfo, Packet, 转换器
    registry/        # NodeRegistry, 宏
    memory/          # FrameBuffer
  modules/           # 示例节点（注册到 Python）
  bindings/          # pybind11 绑定
python/
  easywork/          # Python API + 扩展模块
tests/               # pytest 测试
doc/                 # 深入文档
extern/              # Taskflow（头文件）
CMakeLists.txt
```

## 测试

```bash
PYTHONPATH=python python -m pytest tests
```

Runtime-only C++ 测试：

```bash
ctest --test-dir build_rt
```

## 文档

- `doc/en/runtime_core.md`
- `doc/en/runtime_types.md`
- `doc/en/runtime_registry.md`
- `doc/en/ast_control_flow_design.md`
- `doc/build.md`
- `doc/cross_build.md`
- `doc/ir_schema.md`
- `doc/runtime_logging.md`
- `doc/error_codes.md`
- `doc/cn/runtime_core_cn.md`
- `doc/cn/runtime_types_cn.md`
- `doc/cn/runtime_registry_cn.md`
- `doc/cn/ast_control_flow_design_cn.md`

## Roadmap

- 基于 AST 的 `for/while` 控制流构图。
- 分布式图执行能力。
- 图结构可视化与调试工具。
