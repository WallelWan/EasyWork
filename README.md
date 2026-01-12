# EasyWork Project Documentation

## 1. Project Overview

EasyWork is a computational graph execution framework based on Taskflow static scheduling. It is designed to provide high-performance, type-safe, and easily extensible capabilities for building heterogeneous computation flows.

Key Features:
- **C++ Runtime**: High-performance node system and scheduling engine, implementing static graph scheduling based on Taskflow.
- **Python API**: Concise data flow graph construction interface supporting type checking and automatic topology building.
- **Type System**: `Value` type-erased container and `TypeInfo` reflection mechanism, supporting compile-time and runtime type safety.
- **Heterogeneous Method Support**: Nodes support multiple input/control methods with different signatures (C++20 reflection mechanism).
- **Extensibility**: Easily register new nodes via C++20 factory patterns and macros.

## 2. Project Structure

```
prototype/
├── src/                    # C++ Source Code
│   ├── runtime/            # Runtime Core
│   │   ├── core.h          # Core Execution Engine (Taskflow integration, BaseNode implementation)
│   │   ├── type_system.h   # Type System (TypeInfo, Value, Packet)
│   │   ├── node_registry.h # Node Registration Mechanism & C++20 Factory Pattern
│   │   ├── macros.h        # Macro Definitions (EW_ENABLE_METHODS, EW_REGISTER_NODE)
│   │   ├── modules.h       # Module Aggregation Header
│   │   └── memory/         # Memory Management
│   │       └── frame.h     # FrameBuffer Data Structure (Supports Python Buffer Protocol)
│   ├── bindings/           # Python Bindings
│   │   └── bindings.cpp    # Pybind11 Binding Code
│   └── module/             # Node Implementation Examples
│       └── example_typed_nodes.h
├── python/                 # Python Package
│   └── easywork/
│       ├── __init__.py     # Main API (Pipeline, NodeWrapper)
│       └── *.so            # Compiled C++ Extension
├── tests/                  # Test Files
├── extern/                 # External Dependencies (Taskflow, etc.)
└── CMakeLists.txt          # Build Configuration
```

## 3. Architecture and Execution Model

### 3.1 Scheduling Engine: Taskflow

The project uses **Taskflow** as the underlying scheduling engine:
- **Static Graph Optimization**: The Taskflow graph is built once during `Pipeline.run()`, resulting in extremely low runtime scheduling overhead.
- **Concurrent Execution**: Utilizing Taskflow's `executor` to automatically manage thread pools and task dependencies.

### 3.2 Node Lifecycle

Each node inherits from `BaseNode` and has a complete lifecycle:
1. **Build**: Creates a `tf::Task` in the Taskflow graph.
2. **Connect**: Establishes `precede/succeed` dependency relationships between tasks.
3. **Activate**: Activates the node (e.g., Source node prepares data).
4. **Open(args, kwargs)**: Resource initialization (user-overridable).
5. **Process**: Data processing loop (automatically scheduled via `RunDispatch` or `RunSourceLoop` by the framework).
6. **Close()**: Resource release (user-overridable).

### 3.3 Four-Stage Execution Flow (Pipeline.run)

The Python side `Pipeline.run()` automates the following process:
1. **Reset & Trace**: Clears old connection states and executes the user-defined `construct()` to determine node connection topology.
2. **Build**: Instantiates C++ node tasks within the Taskflow graph.
3. **Connect**: Establishes dependency edges between Taskflow tasks.
4. **Execute**: Starts the Taskflow Executor to run the computation graph.

> Note: `Pipeline.validate()` is an independent, optional step used for static type checking before execution.

### 3.4 Scheduling and Buffer Control

Each node supports fine-grained input method scheduling configuration:

- **Method Ordering**: Customize the check order of input ports via `set_method_order(["left", "right", "forward"])`. Default is `forward` executed last.
- **Input Synchronization**: Enable timestamp alignment via `set_method_sync(method, True)`. The method triggers only when all input ports have data with the same timestamp.
- **FIFO Buffering**: Limit input buffer length via `set_method_queue_size(method, size)`.

## 4. Type System and Data Flow

### 4.1 TypeInfo and Type Safety

The framework generates `TypeInfo` using C++ RTTI and template metaprogramming. `Pipeline.validate()` on the Python side queries C++ node metadata (MethodMeta) to check if upstream output types strictly match downstream input parameter types.

### 4.2 Packet and Timestamps

Data is passed between nodes as `Packet` objects:
- **Payload**: Carries a `Value` of any type (supports basic types, std::vector, custom structs, etc.).
- **Timestamp**: Nanosecond-level timestamp, used for synchronization mechanisms like `SyncBarrier`.

### 4.3 Automatic Tuple Handling

- **C++ Side**: Use `RegisterTupleType<std::tuple<...>>()` to register Tuple types.
- **Python Side**: Supports native unpacking `a, b = node.read()`. The framework automatically inserts `TupleGetNode` to implement splitting without extra coding.

### 4.4 Automatic Type Conversion

EasyWork integrates `pybind11`'s powerful type conversion mechanism, enabling fully automatic bidirectional conversion between Python objects and C++ types.

- **Complex Type Support**: In addition to basic types (`int`, `float`, `str`), you can now directly pass Python `list` to C++ `std::vector`, `dict` to `std::map`, etc.
- **Custom Objects**: Any C++ class registered via `pybind11` can be automatically converted from its Python object representation.

**Example: Passing a List to C++ Vector**

C++ Definition:
```cpp
class VectorSum : public BaseNode<VectorSum> {
public:
    int forward(std::vector<int> vec) {
        return std::accumulate(vec.begin(), vec.end(), 0);
    }
    EW_ENABLE_METHODS(forward)
};
```

Python Call:
```python
node = ew.module.VectorSum()
# Python list [1, 2, 3] is automatically converted to std::vector<int>
result = node([1, 2, 3]) 
print(result) # Output: 6
```

## 5. C++ Node Development

### 5.1 Defining Nodes (New Syntax)

Inherit from `BaseNode<Derived>`. Generic parameters for input/output types are no longer needed. Use the `EW_ENABLE_METHODS` macro to automatically export methods and generate reflection information.

```cpp
#include "runtime/core.h"
#include "runtime/node_registry.h"

using namespace easywork;

class MyMathNode : public BaseNode<MyMathNode> {
public:
    // Method 1: Main processing logic (int -> int)
    int forward(int input) {
        return input * 2;
    }

    // Method 2: Configuration method (float -> void)
    void set_scale(float scale) {
        // ... update internal state
    }

    // Must explicitly export all methods to be exposed to Python
    EW_ENABLE_METHODS(forward, set_scale)
};
```

### 5.2 Heterogeneous Method Support

Nodes can contain multiple methods with completely different signatures. The framework automatically handles parameter unpacking and type conversion:
- `forward(Image img)`
- `config(std::string key, int value)`
- `reset()`

The framework automatically generates invoker glue code to convert `std::vector<Packet>` into native C++ parameters and call the corresponding method.

### 5.3 Dynamic Registration

Use the `EW_REGISTER_NODE` macro to register the node so it can be created in Python. Supports defining constructor arguments (parameter names and default values).

```cpp
// Register MyMathNode with name "MyMath", no arguments
EW_REGISTER_NODE(MyMathNode, "MyMath")

// Register a node with arguments
class Scaler : public BaseNode<Scaler> {
public:
    Scaler(int factor) : factor_(factor) {}
    int forward(int x) { return x * factor_; }
    EW_ENABLE_METHODS(forward)
private:
    int factor_;
};

// Register specifying argument name "factor" and default value 1
EW_REGISTER_NODE(Scaler, "Scaler", Arg("factor", 1))
```

## 6. Python API Manual

### 6.1 Defining a Pipeline

```python
import easywork as ew

class MyPipeline(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.src = ew.module.NumberSource(start=0, max=10)
        self.proc = ew.module.MyMath()
        self.sink = ew.module.NumberSink()

    def construct(self):
        # Default connection to forward method
        self.proc(self.src())
        
        # Connection to specific method
        self.proc.set_scale(ew.module.ConfigProvider().read())
        
        # Chained connection
        self.sink(self.proc.read())
```

### 6.2 Running and Monitoring

`Pipeline.run()` is highly robust; it automatically cleans up old states and rebuilds the topology before each run.

```python
pipeline = MyPipeline()
pipeline.open()

# Optional: Perform static type checking
# pipeline.validate() 

# Automatically builds topology and executes
# Safe to call multiple times; state is reset each time to ensure no side effects
pipeline.run() 

pipeline.close()
```

### 6.3 Performance Monitoring Interface

- `ew.get_method_dispatch_counts()`: Get method dispatch statistics (for debugging).
- `ew.get_small_tracked_live_count()`: Monitor object lifecycle.

### 6.4 Immediate Execution vs Graph Construction (Eager vs Tracing)

EasyWork supports two operation modes, switching automatically based on context:

1.  **Eager Mode**: Outside of a Pipeline context, calling node methods executes the corresponding C++ function immediately and returns the result. Useful for debugging and unit testing.
2.  **Tracing Mode**: Within `construct()` or a `with pipeline:` block, calling node methods records the topology connection and returns a `Symbol`.

```python
import easywork as ew

multiplier = ew.module.MultiplyBy(factor=3)

# --- Eager Mode (Direct Execution) ---
# Executes C++ logic directly, returns the result
result = multiplier(10) 
print(f"Result: {result}")  # Output: 30

# --- Tracing Mode (Graph Construction) ---
pipeline = ew.Pipeline()
with pipeline:
    # Explicitly enter tracing mode
    src = ew.module.NumberSource(start=1, max=5)
    # Returns a Symbol instead of a value
    symbol = multiplier(src.read())
    
pipeline.run()
```

## 7. Advanced Features

### 7.1 SyncBarrier

`SyncBarrier` is a special built-in node for multi-channel input timestamp alignment. It buffers inputs from multiple ports until a set of data within the timestamp tolerance is found, packaging them into a Tuple output.

```cpp
// Usage on C++ side
auto barrier = std::make_shared<SyncBarrier<int, float>>(1000000); // 1ms tolerance
```

### 7.2 Zero-Copy FrameBuffer

Provides a `FrameBuffer` structure supporting the Python Buffer Protocol. Images can be processed in C++ via OpenCV, and the data pointer is directly exposed to Python (NumPy), avoiding memory copies.

## 8. Build and Environment

- **Compiler**: Supports C++20 (GCC 10+, Clang 12+)
- **Dependencies**: Taskflow, OpenCV (optional), pybind11
- **Build**:
  ```bash
  mkdir build && cd build
  cmake ..
  make
  ```

## 9. Future Roadmap

- **AST Parsing & Native Control Flow**: Implement AST analysis (similar to Numba/Triton) to support native Python `if/else`, `for/while` syntax within graph construction, replacing the temporary `ew.If(...)` syntax sugar.
- **Distributed Execution**: Extend Taskflow integration to support distributed graph execution across multiple nodes.
- **Enhanced Visualization**: Provide tools to visualize the generated execution graph for easier debugging and optimization.
