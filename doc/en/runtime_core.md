# Runtime Core Architecture

The Runtime Core (`src/runtime/core`) is the heart of EasyWork, responsible for the graph execution engine, node lifecycle management, and data dispatching. It is built on top of [Taskflow](https://taskflow.github.io/) to achieve high-performance static scheduling.

## 1. Execution Graph

The `ExecutionGraph` class manages the global state of the pipeline execution.

- **Taskflow Integration**: It holds a `tf::Taskflow` object where the static computation graph is built, and a `tf::Executor` to run it.
- **State Management**: It maintains the running state (`keep_running` flag) allowing nodes to signal a stop (e.g., a Source node finishing its sequence).

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
4.  **Synchronization**: If enabled (`SyncBarrier` logic), checks timestamp alignment across inputs.
5.  **Type Conversion**: Applies registered `AnyCaster` converters if the upstream type doesn't match the method argument type.
6.  **Invoke**: Calls the `MethodInvoker`.
7.  **Timestamping**: If the method returns a value (e.g., Source `forward`), ensures it has a valid timestamp (inheriting from inputs or generating `NowNs()`).

## 4. Packet System

Data is exchanged using `Packet` objects, which serve as a universal container.
- **std::any Payload**: Stores the actual data in a type-safe, type-erased manner.
- **Shared Ownership**: Uses `std::shared_ptr` to allow zero-copy fan-out to multiple downstream nodes.
- **Timestamp**: Carries a nanosecond-level timestamp for synchronization.
