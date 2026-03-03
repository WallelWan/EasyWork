# EasyWork

EasyWork is a C++20 + Python runtime for building heterogeneous, type-safe computation graphs. It uses Taskflow for static scheduling, exposes nodes to Python via pybind11, and provides a mixed eager/tracing API for fast prototyping and validation.

## Highlights

- **Static scheduling with Taskflow**: Build once, execute repeatedly with low runtime overhead.
- **Heterogeneous methods**: A node can expose multiple methods with different signatures.
- **Type-checked connections**: Connect-time checks with precompiled converters.
- **Python-first workflow**: Eager execution outside pipelines; tracing inside pipelines.
- **Zero-copy data support**: `Packet` uses `std::any` + shared ownership; `FrameBuffer` supports Python buffer protocol.
- **AST control flow (strict)**: Native `if/else` translated to efficient mux-based routing with compile-time checks.
- **Strict GraphSpec v1 contract**: `schema_version` is required; edges must use method names; legacy `method_id` and `method_config.sync` are rejected.
- **Unified dispatch core**: `BaseNode` and `PyNode` share the same dispatch planning/execution path.
- **Runtime core modularized**: `core.h` is now an umbrella over split headers (`ids.h`, `execution_graph.h`, `node.h`, `base_node.h`, `executor.h`, etc.).

## Quick Start

### Build

Requirements:

- C++20 compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.15+
- Python 3.8+ (only when `EASYWORK_BUILD_PYTHON=ON`)
- OpenCV (only when `EASYWORK_WITH_OPENCV=ON`)

```bash
# Runtime-only (no Python/OpenCV)
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt

# Full build
cmake -S . -B build_full -DEASYWORK_BUILD_PYTHON=ON -DEASYWORK_WITH_OPENCV=ON
cmake --build build_full
```

The Python extension `easywork_core` is emitted to `python/easywork` when Python build is enabled.
See `doc/build.md` and `doc/cross_build.md` for build matrix and cross compilation.

### Python usage

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
pipeline.validate()  # optional but recommended
pipeline.open()      # required before run()
pipeline.run()
pipeline.close()
```

### Graph export/import (Python -> JSON -> C++)

Export a C++-only pipeline to JSON:

```python
pipeline = MyPipeline()
pipeline.validate()
pipeline.export("graph.json")
```

Load the JSON in C++ and run it:

```cpp
#include "runtime/core/graph_build.h"

auto graph = easywork::GraphBuild::FromJsonFile("graph.json");
graph->Run();
```

Notes:

- Export fails if the graph contains Python nodes or internal nodes (e.g., tuple-unpack helpers).
- Constructor args/kwargs must be primitive JSON types (bool/int/float/string).
- Node open/close arguments are not serialized in GraphSpec.
- Runtime loader requires `schema_version == 1`.
- Legacy edge fields `from.method_id` / `to.method_id` are not accepted.
- `method_config.sync` is no longer accepted.
- Use `scripts/migrate_graph_ir_v1.py` to migrate legacy specs to v1.

### Runtime-only executables

We ship runtime-only executables that do not require Python:

```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
./build_rt/easywork_runtime_example
./build_rt/easywork-run --graph graph.json
./build_rt/easywork-run --graph graph.json --log-level info --log-format json --log-file /tmp/easywork.log
```

For C++ tests:
```bash
ctest --test-dir build_rt
```

### Eager mode (no pipeline context)

```python
multiplier = ew.module.MultiplyBy(factor=5)
result = multiplier(10)  # executes immediately, returns int
```

## Execution Model

- **Eager mode**: Calling a node outside a pipeline executes immediately and returns a Python value.
- **Tracing mode**: Inside `Pipeline.construct()` or `with pipeline:` blocks, node calls return `Symbol` objects and build graph connections.
- **Run lifecycle**: `validate()` builds topology and checks types; `run()` builds Taskflow tasks, connects edges, and executes; repeated `run()` resets the graph internally.
- **Open/close**: Nodes must be opened before `run()`. `Node.open()`/`Node.close()` only accept positional args (no kwargs) and enforce argument counts.
- **Error policy API**: `Pipeline.set_error_policy(...)` only accepts `_core.ErrorPolicy` enum values.

## Type System & Conversion

- **TypeInfo**: Lightweight wrapper around `std::type_info` with demangled names.
- **Packet**: `std::any` payload + timestamp; empty packets represent void.
- **Converters**: Connection-time conversion uses `TypeConverterRegistry` with registered arithmetic promotions and Python converters (via pybind11).
- **Tuple support**: Tuple return types are auto-registered for Python unpacking.
- **Mux safety**: All inputs wired via `MuxSymbol` must share the same type, and a given input index cannot mix mux and direct connections.

## Validation Notes

- **IfNode conditions**: `if` conditions in `construct()` must be `bool` or `int` (including int64). Invalid types fail at graph construction.
- **Mux control type**: `MuxSymbol` control input must output `bool`/`int`; mismatched types are rejected during graph construction.
- **Type validation**: Validation accepts exact matches and registered converters; if the core extension does not expose converters, validation is strict.
- **NumberSource step**: `step` must be a non-zero integer; invalid values raise at node creation.

## C++ Node Development

Define nodes by inheriting `BaseNode<T>`, exporting methods with `EW_ENABLE_METHODS`, and registering with `EW_REGISTER_NODE`.

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

Notes:

- Each method signature is reflected and type-checked at connect time.
- `void` methods return empty packets; they cannot be wired into consumers expecting data.
- Methods can be prioritized and buffered via `set_method_order` and `set_method_queue_size`.

## Project Layout

```
src/
  runtime/
    core/            # ExecutionGraph, Node, dispatch logic
    types/           # TypeInfo, Packet, converters
    registry/        # NodeRegistry, macros
    memory/          # FrameBuffer
  modules/           # Example nodes (registered for Python)
  bindings/          # pybind11 bindings
python/
  easywork/          # Python API + extension module
tests/               # Pytest-based tests
doc/                 # Detailed runtime docs
extern/              # Taskflow (header-only)
CMakeLists.txt
```

## Tests

```bash
PYTHONPATH=python python -m pytest tests
```

Runtime-only C++ tests:

```bash
ctest --test-dir build_rt
```

## Documentation

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

- AST-driven control flow for `for/while`.
- Distributed execution on multi-node backends.
- Graph visualization tools for debugging and profiling.
