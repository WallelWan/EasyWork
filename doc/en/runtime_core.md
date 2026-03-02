# Runtime Core Architecture

The Runtime Core (`src/runtime/core`) is the heart of EasyWork, responsible for the graph execution engine, node lifecycle management, and data dispatching. It is built on top of [Taskflow](https://taskflow.github.io/) to achieve high-performance static scheduling.

## 1. Execution Graph

The `ExecutionGraph` class manages the global state of the pipeline execution.

- **Taskflow Integration**: It holds a `tf::Taskflow` object where the static computation graph is built, and a `tf::Executor` to run it.
- **State Management**: It maintains the running state (`keep_running` flag) allowing nodes to signal a stop (e.g., a Source node finishing its sequence).
- **Error Policy**: Supports `FailFast` (stop immediately) or `SkipCurrentData` (drop current data and continue) at runtime.
- **Graph Locking**: The graph is locked while running to prevent connection mutation.

## 2. Node Architecture

The node system is designed around the `Node` base class and the `BaseNode<Derived>` template, providing a Type-Erasure + CRTP (Curiously Recurring Template Pattern) hybrid architecture.

### 2.1 Node Base Class
`Node` is the type-erased interface used by the runtime engine.
- **Buffers**: Manages input `port_buffers_` for data buffering.
- **Upstreams**: Stores `UpstreamConnection` information for graph dependency building.
- **Reflection**: Provides virtual methods like `get_type_info()` and `invoke()` for runtime introspection.
- **Configuration**: Handles method-specific settings like `SetMethodOrder`, `SetMethodSync`, and `SetMethodQueueSize`.

### 2.2 BaseNode Template
`BaseNode<Derived>` implements the type-safe logic using CRTP.
- **Task Creation**: Automatically creates the `tf::Task` in `build()`.
- **Unified Dispatch**: All nodes (including Source nodes) run `RunDispatch`.
- **Source Handling**: If a node has a parameter-less `forward` method, `RunDispatch` automatically handles timestamp generation when `forward` is invoked.

## 3. Dispatch Mechanism

EasyWork supports **Heterogeneous Methods**, meaning a single node can have multiple entry points (methods) with different signatures.

### 3.1 Invoker System
The framework generates `MethodInvoker` functions (type-erased wrappers) for every exposed method in a node.
- **Signature**: `std::function<Packet(Node*, const std::vector<Packet>&)>`
- **Function**: It takes a generic list of `Packet`s, casts them to the specific C++ types required by the method, calls the method, and wraps the result back into a `Packet`.

### 3.2 Dispatch Logic (`RunDispatch`)
The `RunDispatch` loop acts as the local scheduler for each node:
1.  **Buffer Inputs**: Moves data from upstream nodes into local input buffers.
2.  **Check Order**: Iterates through methods according to the configured priority (`method_order_`).
3.  **Check Availability**: Verifies if enough data is available in the buffers for the method's arguments.
4.  **Type Conversion**: Applies registered `AnyCaster` converters if the upstream type doesn't match the method argument type.
5.  **Invoke**: Calls the `MethodInvoker`.
6.  **Timestamping**: If the method returns a value (e.g., Source `forward`), ensures it has a valid timestamp (inheriting from inputs or generating `NowNs()`).

## 4. Packet System

Data is exchanged using `Packet` objects, which serve as a universal container.
- **std::any Payload**: Stores the actual data in a type-safe, type-erased manner.
- **Shared Ownership**: Uses `std::shared_ptr` to allow zero-copy fan-out to multiple downstream nodes.
- **Timestamp**: Carries a nanosecond-level timestamp for synchronization.

## 5. Graph Construction Constraints

- **IfNode condition types**: Conditions must output `bool` or `int` (including int64). Invalid types are rejected during graph construction.
- **Mux control types**: Control packets must be `bool` or `int`; unmapped control values are treated as errors.
- **Repeated runs**: The Taskflow graph is reset between runs, but node-level state is user-controlled; nodes should be written to handle re-entry if reused across runs.

## 6. GraphBuild (C++ Builder) and GraphSpec

EasyWork provides a C++ graph builder that can load a JSON graph spec (GraphSpec) exported from Python.

### 6.1 C++ GraphBuild

```cpp
#include "runtime/core/graph_build.h"

auto graph = easywork::GraphBuild::FromJsonFile("graph.json");
graph->Run();
```

`GraphBuild` builds and runs graphs using registered C++ nodes. It uses the same runtime core as Python.

### 6.2 GraphSpec JSON

GraphSpec is a JSON document with nodes, edges, mux routing, and method configuration.

Key fields:

- `nodes`: list of `{id, type, args, kwargs}`
- `edges`: list of `{from:{node_id, method}, to:{node_id, method, arg_idx}}`
- `mux`: list of `{consumer_id, method, arg_idx, control_id, map}`
- `method_config`: list of `{node_id, order}` or `{node_id, method, sync}` or `{node_id, method, queue_size}`

Limitations:

- Export fails if the graph contains Python nodes or internal helper nodes.
- Constructor args/kwargs must be primitive JSON types (bool/int/float/string).
- Node open/close arguments are not serialized in GraphSpec.

## 7. Runtime-only Build Contract

The runtime path can be built without Python/pybind11/OpenCV:

```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
ctest --test-dir build_rt
```

You can execute exported graph JSON directly:

```bash
./build_rt/easywork-run --graph graph.json
```
