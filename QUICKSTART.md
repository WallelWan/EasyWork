# EasyWork Quick Start

## What is EasyWork?

EasyWork is a **high-performance computer vision pipeline framework** that combines:
- C++20 with Intel OneTBB for parallel stream processing
- Python for flexible pipeline definition
- Zero-copy data sharing between C++ and Python
- Automatic node registration via factory pattern

## Quick Example

```python
import easywork as ew
import cv2
import numpy as np

class MyApp(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.cam = ew.module.CameraSource(device_id=-1)  # Mock mode
        self.proc = ew.PyFunc(self.process_frame)
        self.sink = ew.module.NullSink()

    def process_frame(self, frame):
        # Zero-copy numpy access to C++ frame
        img = np.array(frame, copy=False)
        cv2.rectangle(img, (100, 100), (300, 300), (255, 255, 0), 2)
        return frame

    def construct(self):
        x = self.cam.read()
        y = self.proc(x)
        self.sink.consume(y)

if __name__ == "__main__":
    app = MyApp()
    app.run()
```

## Project Structure

```
prototype/
├── src/runtime/
│   ├── core_tbb.h          # Core TBB graph architecture
│   ├── node_registry.h     # Factory registration (C++20 magic)
│   ├── modules.h           # Include all node modules
│   └── module/             # Node implementations (self-registering)
│       ├── camera_source.h
│       ├── canny_filter.h
│       ├── null_sink.h
│       ├── video_writer_sink.h
│       └── pyfunc_node.h
├── python/easywork/        # Python API
└── tests/                  # Integration tests
```

## Available Nodes

| Node | Type | Parameters | Description |
|------|------|------------|-------------|
| `CameraSource` | InputNode | `device_id: int = -1` | Camera capture or mock mode |
| `CannyFilter` | FunctionNode | None | Canny edge detection |
| `NullSink` | FunctionNode | None | Testing sink (no-op) |
| `VideoWriterSink` | FunctionNode | `filename: str = "output.avi"` | Write to video file |
| `PyFunc` | FunctionNode | `func: Callable` | Wrap Python function |

## Adding New Nodes

### 1. Create C++ Node File

**`src/runtime/module/my_node.h`**:
```cpp
#pragma once
#include "../core_tbb.h"
#include "../node_registry.h"

namespace easywork {

class MyNode : public FunctionNode {
public:
    MyNode(int param) : param_(param) {}

    Frame forward(Frame input) override {
        // Your processing here
        return input;
    }

private:
    int param_;
};

// Self-registration (one line!)
EW_REGISTER_NODE_1(MyNode, "MyNode", int, param, 42)

} // namespace easywork
```

### 2. Update modules.h

```cpp
#include "module/my_node.h"
```

### 3. Rebuild

```bash
cd build && make -j$(nproc)
```

### 4. Use in Python

```python
node = ew.module.MyNode(param=100)
```

## Key Features

### 1. Self-Registering Nodes
Each node file handles its own factory registration:
- ✅ No central registry to maintain
- ✅ Changes in one place
- ✅ Compile-time type checking

### 2. Unified `forward()` Interface

```cpp
// InputNode (source)
Frame forward(tbb::flow_control* fc = nullptr) override;

// FunctionNode (processor/sink)
Frame forward(Frame input) override;
```

### 3. Automatic Parameter Extraction

Supports both positional and keyword arguments:

```python
# All of these work:
cam1 = ew.module.CameraSource(0)
cam2 = ew.module.CameraSource(device_id=0)
cam3 = ew.module.CameraSource()  # uses default (-1)
```

### 4. Zero-Copy Frame Sharing

```python
def process(frame):
    # No data copy! Direct access to C++ memory
    img = np.array(frame, copy=False)
    # ... modify img ...
    return frame  # Changes visible to C++ nodes
```

## Architecture Details

See [ARCHITECTURE.md](ARCHITECTURE.md) for:
- Detailed design principles
- C++20 features used (Concepts, NTTP, constexpr)
- TBB flow_graph integration
- Memory management
- Adding multi-parameter nodes

## Build Instructions

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Run Tests

```bash
# From project root
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libstdc++.so.6 \
    PYTHONPATH=python:$PYTHONPATH \
    python tests/test_phase2_class.py
```

## Design Philosophy

1. **Two node types**: InputNode (sources) and FunctionNode (processors/sinks)
2. **Single method**: All nodes use `forward()` for consistency
3. **Factory pattern**: Automatic registration with C++20 compile-time magic
4. **Self-contained**: Each module file owns its registration
5. **Zero-copy**: Shared memory between C++ and Python for performance
