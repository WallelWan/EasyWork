# EasyWork 类型系统 - 快速参考

## 🚀 快速开始

### 1. 定义新的类型化节点

#### 输入节点（无输入，单个输出）
```cpp
// src/runtime/module/my_nodes.h
class IntSource : public TypedInputNode<IntSource, int> {
public:
    IntSource(int start, int max)
        : current_(start), max_(max) {}

    // 注意：不要使用 override 关键字（CRTP 模式）
    int forward(tbb::flow_control* fc) {
        if (current_ > max_) {
            fc->stop();
            return 0;
        }
        return current_++;
    }

private:
    int current_;
    int max_;
};

// 注册节点（根据参数数量选择宏）
EW_REGISTER_NODE_2(IntSource, "IntSource",
                  int, start, 0,
                  int, max, 100)
```

#### 单输入函数节点
```cpp
class Doubler : public TypedFunctionNode<Doubler, int, int> {
public:
    int forward(int input) {  // 不要用 override
        return input * 2;
    }
};

EW_REGISTER_NODE(Doubler, "Doubler")
```

#### 多输入函数节点
```cpp
class PairSum : public TypedMultiInputFunctionNode<PairSum, int, int, int> {
public:
    int forward(int a, int b) {  // 两个参数
        return a + b;
    }
};

// 注意：多输入节点注册需要自定义实现
```

### 2. Python 端使用

```python
import easywork as ew

class MyPipeline(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.source = ew.module.IntSource(start=0, max=10)
        self.doubler = ew.module.Doubler()

    def construct(self):
        x = self.source.read()    # int
        y = self.doubler(x)       # int -> int

pipeline = MyPipeline()
pipeline.validate()  # 可选：类型检查
pipeline.run()
```

---

## 📚 核心 API 参考

### C++ API

#### TypeInfo
```cpp
TypeInfo info = TypeInfo::create<int>();
std::cout << info.type_name;     // "i" (mangled name)
size_t hash = info.type_hash;    // 编译时哈希
bool same = (info == TypeInfo::create<int>());  // true
```

#### NodeTypeInfo
```cpp
NodeTypeInfo type_info = node->get_type_info();

// 查询输入类型
for (const auto& t : type_info.input_types) {
    std::cout << t.type_name;
}

// 查询输出类型
for (const auto& t : type_info.output_types) {
    std::cout << t.type_name;
}

// 类型检查
bool accepts = type_info.accepts_input(TypeInfo::create<int>());
```

#### Value
```cpp
// 构造
Value int_val(42);
Value frame_val(frame_ptr);
Value tuple_val(std::make_tuple(1, 2.0f, "hello"));

// 转换
int i = int_val.cast<int>();
Frame f = frame_val.cast<Frame>();
auto tup = tuple_val.cast<std::tuple<int, float, std::string>>();

// 查询类型
TypeInfo t = int_val.type();
```

### Python API

#### Symbol
```python
# 创建 Symbol（通常自动创建）
symbol = ew.Symbol(node)
indexed_symbol = ew.Symbol(node, tuple_index=0)

# Tuple 解包
a, b, c = symbol  # 自动创建带索引的 Symbol

# 查询
type_info = symbol.producer_node.type_info
index = symbol.tuple_index  # None 或 0, 1, 2...
```

#### NodeWrapper
```python
# 创建节点
node = ew.module.IntSource(0, 10)

# 查询类型信息
type_info = node.raw.type_info
input_types = [t.name for t in type_info.input_types]
output_types = [t.name for t in type_info.output_types]

# 连接
result = node(input_symbol)  # 单输入
# result = node(a, b, c)     # 多输入（待实现）
```

#### Pipeline
```python
class MyPipeline(ew.Pipeline):
    def construct(self):
        # 定义节点连接
        x = self.source.read()
        y = self.proc(x)

pipeline = MyPipeline()

# 类型检查（可选）
pipeline.validate()

# 运行
pipeline.run()
```

---

## 🔧 注册宏速查

### EW_REGISTER_NODE
```cpp
// 0 参数节点
class SimpleNode : public TypedFunctionNode<SimpleNode, int, int> {
public:
    int forward(int input) { return input; }
};
EW_REGISTER_NODE(SimpleNode, "SimpleNode")
```

### EW_REGISTER_NODE_1
```cpp
// 1 参数节点
class Node1 : public TypedFunctionNode<Node1, int, int> {
public:
    Node1(int factor) : factor_(factor) {}
    int forward(int input) { return input * factor_; }
private:
    int factor_;
};
EW_REGISTER_NODE_1(Node1, "Node1",
                  int, factor, 2)
```

### EW_REGISTER_NODE_2
```cpp
// 2 参数节点
class Node2 : public TypedInputNode<Node2, int> {
public:
    Node2(int start, int max) : current_(start), max_(max) {}
    int forward(tbb::flow_control* fc) { /* ... */ }
private:
    int current_, max_;
};
EW_REGISTER_NODE_2(Node2, "Node2",
                  int, start, 0,
                  int, max, 100)
```

### EW_REGISTER_NODE_3
```cpp
// 3 参数节点
class Node3 : public TypedInputNode<Node3, int> {
public:
    Node3(int start, int max, int step)
        : current_(start), max_(max), step_(step) {}
    int forward(tbb::flow_control* fc) { /* ... */ }
private:
    int current_, max_, step_;
};
EW_REGISTER_NODE_3(Node3, "Node3",
                  int, start, 0,
                  int, max, 100,
                  int, step, 1)
```

---

## 🎯 常见模式

### 模式 1：Frame 处理节点
```cpp
class BlurFilter : public TypedFunctionNode<BlurFilter, Frame, Frame> {
public:
    Frame forward(Frame input) {
        Frame output = make_frame(input->width, input->height);
        cv::GaussianBlur(input->mat, output->mat, cv::Size(5, 5), 0);
        return output;
    }
};
EW_REGISTER_NODE(BlurFilter, "BlurFilter")
```

### 模式 2：类型转换节点
```cpp
class IntToString : public TypedFunctionNode<IntToString, int, std::string> {
public:
    std::string forward(int input) {
        return std::to_string(input);
    }
};
EW_REGISTER_NODE(IntToString, "IntToString")
```

### 模式 3：有状态节点
```cpp
class Accumulator : public TypedFunctionNode<Accumulator, int, int> {
public:
    Accumulator() : sum_(0) {}

    int forward(int input) {
        sum_ += input;
        return sum_;
    }

private:
    int sum_;
};
EW_REGISTER_NODE(Accumulator, "Accumulator")
```

### 模式 4：条件输出节点
```cpp
class Filter : public TypedFunctionNode<Filter, int, std::optional<int>> {
public:
    std::optional<int> forward(int input) {
        if (input % 2 == 0) {
            return input;  // 偶数通过
        }
        return std::nullopt;  // 奇数被过滤
    }
};
// 注意：std::optional 需要特殊处理
```

---

## ⚠️ 常见陷阱

### ❌ 错误：使用 override 关键字
```cpp
class BadNode : public TypedFunctionNode<BadNode, int, int> {
public:
    int forward(int input) override {  // ❌ 错误！
        return input;
    }
};
```

### ✅ 正确：不使用 override
```cpp
class GoodNode : public TypedFunctionNode<GoodNode, int, int> {
public:
    int forward(int input) {  // ✅ 正确
        return input;
    }
};
```

**原因**: CRTP 模式使用编译时多态，不是虚函数。

### ❌ 错误：忘记注册节点
```cpp
class MyNode : public TypedFunctionNode<MyNode, int, int> {
public:
    int forward(int input) { return input; }
};
// ❌ 忘记 EW_REGISTER_NODE
```

### ✅ 正确：注册节点
```cpp
class MyNode : public TypedFunctionNode<MyNode, int, int> {
public:
    int forward(int input) { return input; }
};
EW_REGISTER_NODE(MyNode, "MyNode")  // ✅ 必须注册
```

### ❌ 错误：参数类型不匹配
```cpp
class BadNode : public TypedFunctionNode<BadNode, int, int> {
public:
    BadNode(int factor) : factor_(factor) {}
    int forward(int input) { return input * factor_; }
private:
    int factor_;
};
EW_REGISTER_NODE(BadNode, "BadNode")  // ❌ 缺少参数！
```

### ✅ 正确：使用正确的注册宏
```cpp
EW_REGISTER_NODE_1(BadNode, "BadNode", int, factor, 2)  // ✅
```

---

## 📊 类型支持表

| 类型 | C++ 类型 | Python 类型 | Value 存储 | 零拷贝 |
|------|----------|-------------|-----------|--------|
| int | `int` | `int` | 栈缓冲区 | ✅ |
| float | `float` | `float` | 栈缓冲区 | ✅ |
| bool | `bool` | `bool` | 栈缓冲区 | ✅ |
| string | `std::string` | `str` | 栈缓冲区 (SSO) | ✅ |
| Frame | `Frame` (shared_ptr) | `easywork.Frame` | 栈缓冲区 (指针) | ✅ |
| tuple | `std::tuple<T...>` | `tuple` | 堆分配 | ❌ |
| vector | `std::vector<T>` | `list` | 堆分配 | ❌ |
| optional | `std::optional<T>` | `Optional[T]` | 栈缓冲区 | ✅ |

---

## 🔍 调试技巧

### 查看节点类型信息
```python
node = ew.module.IntCounter(0, 10, 1)
type_info = node.raw.type_info

print(f"Input types: {[t.name for t in type_info.input_types]}")
print(f"Output types: {[t.name for t in type_info.output_types]}")
```

### 启用详细日志
```cpp
spdlog::set_level(spdlog::level::debug);  // C++ 端
```

### 追踪 Symbol 索引
```python
def debug_symbol(symbol):
    print(f"Producer: {symbol.producer_node}")
    print(f"Tuple index: {symbol.tuple_index}")
    print(f"Type: {symbol.producer_node.type_info}")
```

---

## 📝 检查清单

### 定义新节点时
- [ ] 继承正确的基类（`TypedInputNode` / `TypedFunctionNode` / `TypedMultiInputFunctionNode`）
- [ ] 模板参数：`<类名, 输入类型, 输出类型>`
- [ ] 实现 `forward()` 方法（**不要**用 `override`）
- [ ] 使用正确的注册宏（参数数量匹配）
- [ ] 注册宏的 Python 名称用字符串
- [ ] 包含 `#include "../node_registry.h"`

### Python 端使用时
- [ ] 调用 `pipeline.validate()` 进行类型检查（可选）
- [ ] 使用 `Symbol` 连接节点（或直接调用）
- [ ] 多返回值用 `a, b, c = node.read()` 解包
- [ ] 检查节点是否注册：`"NodeName" in dir(ew.module)`

---

## 🚨 故障排除

### 问题：编译错误 "marked 'override', but does not override"
**解决**：删除 `override` 关键字（CRTP 模式）

### 问题：节点未注册
**解决**：检查是否调用了 `EW_REGISTER_NODE*` 宏

### 问题：类型转换失败
**解决**：检查输入/输出类型是否匹配，查看 `type_info`

### 问题：Pipeline 挂起
**解决**：
- 确保输入节点在某个条件下调用 `fc->stop()`
- 使用有限计数器或超时机制
- 检查是否有循环依赖

### 问题：找不到节点
**解决**：
- 检查节点是否注册：`"NodeName" in dir(ew.module)`
- 重新编译 C++ 扩展
- 检查宏的 Python 名称拼写

---

**最后更新**: 2025-01-09
**版本**: v0.2.0-alpha
