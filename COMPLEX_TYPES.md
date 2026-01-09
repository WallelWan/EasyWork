# 复杂 Python 类型支持指南

## 概述

EasyWork 节点注册系统**完全支持**复杂 Python 类型作为节点参数，包括：
- ✅ 列表 (list)
- ✅ 元组 (tuple)
- ✅ 字典 (dict)
- ✅ 嵌套容器 (list of lists, dict with list values 等)

## 为什么支持？

节点注册系统使用 `pybind11::cast<T>()` 进行类型转换：

```cpp
// node_registry.h:115
return args[index].cast<T>();  // pybind11 自动转换
```

而 `bindings.cpp` 已包含 STL 支持：

```cpp
#include <pybind11/stl.h>  // 支持 STL 容器类型转换
```

## 支持的类型映射

### 基本类型

| Python 类型 | C++ 类型 | 示例 |
|------------|---------|------|
| `int` | `int`, `int64_t`, `size_t` | `42` |
| `float` | `float`, `double` | `3.14` |
| `str` | `std::string` | `"hello"` |
| `bool` | `bool` | `True` |

### 容器类型

| Python 类型 | C++ 类型 | 示例 |
|------------|---------|------|
| `list` | `std::vector<T>` | `[1, 2, 3]` → `std::vector<int>` |
| `tuple` | `std::tuple<Ts...>` | `(1, "hi", 3.14)` → `std::tuple<int, std::string, double>` |
| `dict` | `std::map<K,V>` | `{"a": 1}` → `std::map<std::string, int>` |
| `dict` | `std::unordered_map<K,V>` | `{"a": 1}` → `std::unordered_map<std::string, int>` |

### 嵌套容器

| Python 类型 | C++ 类型 | 示例 |
|------------|---------|------|
| `list of list` | `std::vector<std::vector<T>>` | `[[1,2], [3,4]]` |
| `dict of list` | `std::map<K, std::vector<V>>` | `{"a": [1,2]}` |
| `list of dict` | `std::vector<std::map<K,V>>` | `[{"a":1}, {"b":2}]` |

## 使用示例

### 示例 1: 节点接收 list 参数

**C++ 节点定义** (`multi_threshold.h`):

```cpp
#pragma once
#include "../core_tbb.h"
#include "../node_registry.h"
#include <opencv2/imgproc.hpp>
#include <vector>

namespace easywork {

class MultiThresholdFilter : public FunctionNode {
public:
    MultiThresholdFilter(std::vector<int> thresholds)
        : thresholds_(thresholds) {}

    Frame forward(Frame input) override {
        auto output = make_frame(input->width, input->height, input->mat.type());
        input->mat.copyTo(output->mat);

        // 应用多个阈值
        for (int thresh : thresholds_) {
            cv::threshold(output->mat, output->mat, thresh, 255, cv::THRESH_BINARY);
        }
        return output;
    }

private:
    std::vector<int> thresholds_;
};

// 注册：list 参数
EW_REGISTER_NODE_1(MultiThresholdFilter, "MultiThresholdFilter",
    std::vector<int>, thresholds, std::vector<int>{100, 200})

} // namespace easywork
```

**Python 使用**:

```python
import easywork as ew

# 方式 1：关键字参数
thresh = ew.module.MultiThresholdFilter(thresholds=[50, 100, 150, 200])

# 方式 2：位置参数
thresh = ew.module.MultiThresholdFilter([50, 100, 150, 200])

# 方式 3：使用默认值 [100, 200]
thresh = ew.module.MultiThresholdFilter()
```

### 示例 2: 节点接收 tuple 参数

**C++ 节点定义** (`roi_extractor.h`):

```cpp
#pragma once
#include "../core_tbb.h"
#include "../node_registry.h"
#include <opencv2/imgproc.hpp>
#include <tuple>

namespace easywork {

class ROIExtractor : public FunctionNode {
public:
    // tuple<int, int, int, int> = (x, y, width, height)
    ROIExtractor(std::tuple<int, int, int, int> roi)
        : roi_(roi) {}

    Frame forward(Frame input) override {
        auto [x, y, w, h] = roi_;
        cv::Rect rect(x, y, w, h);

        auto output = make_frame(w, h, input->mat.type());
        input->mat(rect).copyTo(output->mat);
        return output;
    }

private:
    std::tuple<int, int, int, int> roi_;
};

// 注册：tuple 参数
EW_REGISTER_NODE_1(ROIExtractor, "ROIExtractor",
    std::tuple<int, int, int, int>, roi, std::make_tuple(0, 0, 640, 480))

} // namespace easywork
```

**Python 使用**:

```python
import easywork as ew

# 方式 1：关键字参数
roi = ew.module.ROIExtractor(roi=(100, 100, 320, 240))

# 方式 2：位置参数
roi = ew.module.ROIExtractor((100, 100, 320, 240))

# 方式 3：使用默认值 (0, 0, 640, 480)
roi = ew.module.ROIExtractor()
```

### 示例 3: 节点接收 dict 参数

**C++ 节点定义** (`color_mapper.h`):

```cpp
#pragma once
#include "../core_tbb.h"
#include "../node_registry.h"
#include <opencv2/imgproc.hpp>
#include <map>
#include <string>

namespace easywork {

class ColorMapper : public FunctionNode {
public:
    // map<string, int> = {"red": 0, "green": 1, ...}
    ColorMapper(std::map<std::string, int> color_map)
        : color_map_(color_map) {}

    Frame forward(Frame input) override {
        // 根据 color_map 应用颜色映射
        for (const auto& [name, value] : color_map_) {
            // 应用颜色映射逻辑
        }
        return input;
    }

private:
    std::map<std::string, int> color_map_;
};

// 注册：dict 参数（默认为空 map）
EW_REGISTER_NODE_1(ColorMapper, "ColorMapper",
    std::map<std::string, int>, color_map, std::map<std::string, int>{})

} // namespace easywork
```

**Python 使用**:

```python
import easywork as ew

# 方式 1：关键字参数
mapper = ew.module.ColorMapper(color_map={"red": 0, "green": 1, "blue": 2})

# 方式 2：位置参数
mapper = ew.module.ColorMapper({"red": 0, "green": 1, "blue": 2})

# 方式 3：使用默认值 {}
mapper = ew.module.ColorMapper()
```

### 示例 4: 多个复杂参数 (EW_REGISTER_NODE_2)

**C++ 节点定义** (`complex_filter.h`):

```cpp
#pragma once
#include "../core_tbb.h"
#include "../node_registry.h"
#include <vector>
#include <string>

namespace easywork {

class ComplexFilter : public FunctionNode {
public:
    ComplexFilter(std::vector<std::string> channels,
                 std::vector<int> thresholds)
        : channels_(channels), thresholds_(thresholds) {}

    Frame forward(Frame input) override {
        // 使用 channels 和 thresholds
        return input;
    }

private:
    std::vector<std::string> channels_;
    std::vector<int> thresholds_;
};

// 注册：2 个复杂参数
EW_REGISTER_NODE_2(ComplexFilter, "ComplexFilter",
    std::vector<std::string>, channels, std::vector<std::string>{"R", "G", "B"},
    std::vector<int>, thresholds, std::vector<int>{100, 200})

} // namespace easywork
```

**Python 使用**:

```python
import easywork as ew

# 方式 1：关键字参数
filt = ew.module.ComplexFilter(
    channels=["R", "G", "B"],
    thresholds=[50, 100, 150]
)

# 方式 2：位置参数
filt = ew.module.ComplexFilter(
    ["R", "G", "B"],
    [50, 100, 150]
)

# 方式 3：混合使用
filt = ew.module.ComplexFilter(
    thresholds=[50, 100, 150]  # channels 使用默认值
)

# 方式 4：全部使用默认值
filt = ew.module.ComplexFilter()
```

## 限制和注意事项

### 1. 类型必须匹配

Python 类型必须能转换为对应的 C++ 类型：

```python
# ✅ 正确
node = ew.module.MyNode([1, 2, 3])  # list[int] → std::vector<int>

# ❌ 错误
node = ew.module.MyNode([1, "hello", 3.14])  # heterogeneous list → 编译错误
```

### 2. 需要正确的头文件

确保节点文件包含了所需类型的头文件：

```cpp
#include <vector>      // for std::vector
#include <map>         // for std::map
#include <unordered_map>  // for std::unordered_map
#include <tuple>       // for std::tuple
#include <string>      // for std::string
```

### 3. 复杂类型可能需要特殊处理

某些复杂类型可能需要额外的 pybind11 声明：

```cpp
// 在 bindings.cpp 中（如果遇到问题）
PYBIND11_MAKE_OPAQUE(std::vector<MyCustomType>)
PYBIND11_MAKE_OPAQUE(std::map<std::string, MyCustomType>)
```

但对于常见的 STL 类型（vector, map, tuple），`#include <pybind11/stl.h>` 已足够。

## 性能考虑

### 优点

- ✅ **零拷贝**：pybind11 尽可能使用引用传递
- ✅ **编译期优化**：类型转换在编译期确定
- ✅ **缓存友好**：连续内存布局（对于 vector）

### 缺点

- ⚠️ **转换开销**：复杂类型需要遍历和转换
- ⚠️ **内存占用**：大容器会复制数据

建议：
- 对于小型容器（< 1000 元素）：直接使用
- 对于大型容器：考虑使用指针或共享指针

## 总结

EasyWork 节点注册系统对复杂 Python 类型的支持是**开箱即用**的：

| 特性 | 状态 |
|-----|------|
| 基本类型 (int, float, str, bool) | ✅ 完全支持 |
| 容器类型 (list, tuple, dict) | ✅ 完全支持 |
| 嵌套容器 (list of lists, etc.) | ✅ 完全支持 |
| 自定义类型 | ⚠️ 可能需要额外声明 |
| 性能优化 | ✅ pybind11 自动优化 |

只要在 C++ 中声明正确的 STL 类型，Python 就能传递对应的数据结构！
