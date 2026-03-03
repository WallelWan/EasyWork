# Runtime Core Architecture

The Runtime Core (`src/runtime/core`) is the execution engine for EasyWork. It is built on [Taskflow](https://taskflow.github.io/) and now uses a modular header layout plus a shared dispatch engine across C++ and Python nodes.

## 1. Header Layout

`core.h` is an umbrella include. The implementation is split by responsibility:

- `ids.h`: method id constants and `ErrorPolicy`
- `execution_graph.h`: `ExecutionGraph` global state and error tracking
- `method_reflection.h`: method metadata and invoker factories
- `node.h`: type-erased `Node`, topology state, shared dispatch planner/runner
- `base_node.h`: CRTP `BaseNode<Derived>` implementation and fast-path dispatch
- `executor.h`: execution loop and graph lifecycle helpers

## 2. Execution Graph

`ExecutionGraph` manages:

- Taskflow graph (`tf::Taskflow`) and executor (`tf::Executor`)
- Runtime stop flags (`keep_running`, `skip_current`)
- Error accounting (`last_error`, `error_count`, `last_error_code`)
- Error policy (`FailFast` / `SkipCurrentData`)
- Graph lock state while executing

## 3. Node Model

`Node` is the type-erased runtime interface.

- Holds topology, port mapping, buffers, method config, and output packet.
- Internal mutable state is encapsulated (no public mutable internals).
- Provides controlled accessors for derived classes and bindings.
- `Open/Close` optional behavior is based on `HasMethod(ID_OPEN/ID_CLOSE)`, not string-matching exception text.

## 4. Unified Dispatch Engine

The shared dispatch core lives in `Node`:

- `BuildDispatchPlans(...)`
- `RunDispatchPlans(...)`

Both `BaseNode` and `PyNode` use this engine.

### 4.1 BaseNode path

`BaseNode<Derived>` builds method plans from `method_registry()` and keeps:

- type-safe invoker path (`MethodInvoker`)
- optional fast path (`FastInvoker`) for trivially-copyable signatures

### 4.2 PyNode path

`PyNode` now uses the same dispatch planning/execution loop and only customizes:

- method signature introspection from Python callables
- Python invocation and mux choice decoding for `py::object`
- GIL-safe buffer/output cleanup overrides

## 5. GraphBuild + GraphSpec Contract

`GraphBuild` consumes strict GraphSpec v1:

- `schema_version` is required and must be `1`
- edges must use method names (`from.method`, `to.method`)
- legacy `method_id` fields are rejected
- `method_config.sync` is removed and rejected
- `Connect` validates producer/consumer methods and consumer `arg_idx` bounds

See `doc/ir_schema.md` for the full schema and migration guidance.

## 6. Runtime-only Contract

Runtime-only builds remain supported:

```bash
cmake -S . -B build_rt -DEASYWORK_BUILD_PYTHON=OFF -DEASYWORK_WITH_OPENCV=OFF
cmake --build build_rt
ctest --test-dir build_rt
```

The strict JSON IR can be executed directly:

```bash
./build_rt/easywork-run --graph graph.json
```

