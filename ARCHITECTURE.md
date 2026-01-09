# EasyWork Architecture

## Project Overview

EasyWork is a high-performance computer vision pipeline framework built with:
- **C++20**: Concepts, constexpr, NTTP (Non-Type Template Parameters)
- **Intel OneTBB**: Parallel flow_graph for stream processing
- **OpenCV**: Computer vision operations
- **Python Bindings**: Pybind11 for Python API

## Architecture Design Principles

1. **Two Node Types**: `InputNode` (source) and `FunctionNode` (processor/sink)
2. **Unified Interface**: All nodes use `forward()` method
3. **Factory Pattern**: Automatic registration with C++20 compile-time magic
4. **Zero-Copy**: Frame data shared between C++ and Python via buffer protocol
5. **Self-Contained**: Each module file handles its own registration

## Directory Structure

```
prototype/
├── src/
│   ├── runtime/
│   │   ├── core_tbb.h          # Core TBB graph architecture
│   │   ├── node_registry.h     # Factory registration mechanism
│   │   ├── modules.h           # Unified header for all modules
│   │   ├── memory/
│   │   │   └── frame.h         # Frame buffer with zero-copy protocol
│   │   └── module/             # Node implementations (one per file)
│   │       ├── camera_source.h       # Camera/Mock source
│   │       ├── canny_filter.h        # Canny edge detection
│   │       ├── null_sink.h           # Null sink (testing)
│   │       ├── video_writer_sink.h   # Video file writer
│   │       └── pyfunc_node.h         # Python callable wrapper
│   ├── bindings/
│   │   └── bindings.cpp        # Python bindings (pybind11)
│   └── CMakeLists.txt
├── python/
│   └── easywork/
│       ├── __init__.py         # Python API (factory + wrappers)
│       └── easywork_core*.so   # Compiled C++ extension
├── tests/
│   └── test_phase2_class.py    # Integration test
└── build/                      # CMake build output
```

## Core Components

### 1. Node Base Classes ([core_tbb.h](src/runtime/core_tbb.h))

#### InputNode (Source Nodes)
```cpp
class InputNode : public Node {
    // Uses TBB input_node<Frame>
    virtual Frame forward(tbb::flow_control* fc = nullptr) = 0;
};
```

#### FunctionNode (Processor/Sink Nodes)
```cpp
class FunctionNode : public Node {
    // Uses TBB function_node<Frame, Frame>
    virtual Frame forward(Frame input) = 0;
    // Sink nodes return nullptr to terminate flow
};
```

### 2. Factory Registration ([node_registry.h](src/runtime/node_registry.h))

#### Registration Macros

**For nodes with default constructor (0 parameters):**
```cpp
// In canny_filter.h
EW_REGISTER_NODE(CannyFilter, "CannyFilter")
```

**For nodes with 1 parameter:**
```cpp
// In camera_source.h
EW_REGISTER_NODE_1(CameraSource, "CameraSource",
    int,      // Parameter type
    device_id, // Parameter name (for kwargs)
    -1)       // Default value
```

**For nodes with 2 parameters:**
```cpp
// Example (reserved for future use)
EW_REGISTER_NODE_2(MyNode, "MyNode",
    int, param1, 0,
    std::string, param2, "default")
```

#### How It Works

1. **String Literal NTTP**: C++20 compile-time string hashing
2. **Parameter Extractor**: Auto-generated args/kwargs parsing
3. **Template Specialization**: `NodeCreatorImpl<NodeT>` for custom logic
4. **Global Registrar**: Static objects register at startup

### 3. Memory Management ([memory/frame.h](src/runtime/memory/frame.h))

```cpp
struct FrameBuffer {
    int width, height;
    cv::Mat mat;              // OpenCV matrix (owns data)
    std::vector<int> shape;   // {height, width, channels}
    std::vector<size_t> strides;

    // Zero-copy numpy array conversion
    py::buffer_info buffer();
};
```

### 4. Python API ([python/easywork/__init__.py](python/easywork/__init__.py))

#### Dynamic Module Access
```python
import easywork as ew

# Access C++ nodes via factory
cam = ew.module.CameraSource(device_id=-1)
canny = ew.module.CannyFilter()
sink = ew.module.NullSink()
```

#### Pipeline Definition
```python
class MyApp(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.cam = ew.module.CameraSource(-1)
        self.proc = ew.PyFunc(self.process)
        self.sink = ew.module.NullSink()

    def construct(self):
        x = self.cam.read()
        y = self.proc(x)
        self.sink.consume(y)
```

## Node Modules

### CameraSource ([camera_source.h](src/runtime/module/camera_source.h))
- **Type**: `InputNode`
- **Parameters**: `int device_id` (default: -1)
- **Description**: Captures frames from camera or generates mock frames (Red/Blue/White pattern)

### CannyFilter ([canny_filter.h](src/runtime/module/canny_filter.h))
- **Type**: `FunctionNode`
- **Parameters**: None
- **Description**: Applies Canny edge detection (converts to grayscale first)

### NullSink ([null_sink.h](src/runtime/module/null_sink.h))
- **Type**: `FunctionNode`
- **Parameters**: None
- **Description**: Consumes frames without side effects (for testing)

### VideoWriterSink ([video_writer_sink.h](src/runtime/module/video_writer_sink.h))
- **Type**: `FunctionNode`
- **Parameters**: `std::string filename` (default: "output.avi")
- **Description**: Writes frames to video file (MJPEG, 30 FPS)

### PyFuncNode ([pyfunc_node.h](src/runtime/module/pyfunc_node.h))
- **Type**: `FunctionNode`
- **Parameters**: `pybind11::function func` (Python callable)
- **Description**: Wraps Python function for custom processing (NOT in factory)

## Adding New Nodes

### Step 1: Create Node File

Create `src/runtime/module/my_node.h`:

```cpp
#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"

namespace easywork {

class MyNode : public FunctionNode {
public:
    MyNode(MyParamType param) : param_(param) {}

    Frame forward(Frame input) override {
        // Your processing logic
        return input;
    }

private:
    MyParamType param_;
};

// Factory Registration
EW_REGISTER_NODE_1(MyNode, "MyNode",
    MyParamType, param, default_value)

} // namespace easywork
```

### Step 2: Update modules.h

```cpp
#include "module/my_node.h"
```

### Step 3: Rebuild

```bash
cd build && make -j$(nproc)
```

### Step 4: Use in Python

```python
import easywork as ew

my_node = ew.module.MyNode(param_value)
```

## Build System

### CMake Configuration

```cmake
# C++20 required
set(CMAKE_CXX_STANDARD 20)

# Find dependencies
find_package(OpenCV REQUIRED)
find_package(TBB REQUIRED)
find_package(pybind11 REQUIRED)

# Python extension
pybind11_add_module(easywork_core
    src/bindings/bindings.cpp
    src/runtime/*.h
    src/runtime/memory/*.h
    src/runtime/module/*.h
)

# Link libraries
target_link_libraries(easywork_core PRIVATE
    ${OpenCV_LIBS}
    TBB::tbb
    spdlog
)
```

### Compilation

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Testing

```bash
# Run integration test
cd /path/to/prototype
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libstdc++.so.6 \
    PYTHONPATH=python:$PYTHONPATH \
    python tests/test_phase2_class.py
```

## Design Decisions

### Why Two Node Types?

TBB requires different node types:
- `input_node<Frame>` for data sources (with flow_control)
- `function_node<Frame, Frame>` for processing and sinks

Trying to unify into one type would lose TBB's optimization capabilities.

### Why Distributed Registration?

Each file handles its own registration:
- ✅ Easier maintenance (changes in one place)
- ✅ Clearer ownership (node + registration together)
- ✅ Simpler addition (just copy a node file)
- ❌ ~~Centralized registry~~ (harder to maintain)

### Why Sink Returns nullptr?

Instead of `continue_msg`:
- ✅ More intuitive (no output = termination)
- ✅ Unified interface (all FunctionNodes return Frame)
- ✅ Natural flow control (nullptr stops propagation)

### Why String Literal NTTP?

C++20 feature for compile-time string hashing:
- ✅ Zero runtime overhead (constexpr)
- ✅ Type-safe (compile-time checking)
- ✅ No manual hash calculation
- ❌ IDE support may be limited

## Performance Characteristics

- **Zero-Copy**: Frame data shared between C++ and Python
- **Parallel**: TBB automatic parallelization
- **Lock-Free**: TBB flow_graph uses lock-free queues
- **Cache-Friendly**: Sequential frame processing

## Future Extensions

### Easy Additions

- Add `EW_REGISTER_NODE_3`, `EW_REGISTER_NODE_4` for more parameters
- Add new node types in `module/` directory
- Extend Frame with metadata (timestamps, indices)

### More Complex Changes

- Dynamic topology modification (add/remove nodes during execution)
- Multi-source synchronization (merge multiple inputs)
- Backpressure handling (flow control for slow consumers)

## References

- [Intel OneTBB Documentation](https://oneapi-src.github.io/oneTBB/)
- [Pybind11 Documentation](https://pybind11.readthedocs.io/)
- [C++20 Concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [OpenCV Documentation](https://docs.opencv.org/)
