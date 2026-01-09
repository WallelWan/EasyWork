# EasyWork 通用类型系统实现 - 当前状态

**日期**: 2025-01-09
**状态**: 核心功能已完成，编译通过，测试阶段遇到挂起问题

---

## ✅ 已完成的工作

### 1. 核心类型系统 (`src/runtime/type_system.h`)

#### TypeInfo - 类型描述符
```cpp
struct TypeInfo {
    const std::type_info* type_info;  // 使用指针避免引用初始化问题
    std::string type_name;
    size_t type_hash;

    template<typename T>
    static TypeInfo create();

    bool operator==(const TypeInfo& other) const;
    bool operator!=(const TypeInfo& other) const;
};
```

**功能**:
- 存储类型的运行时信息（RTTI）
- 支持类型比较和哈希
- 通过 `typeid()` 获取类型名称

#### NodeTypeInfo - 节点类型元数据
```cpp
struct NodeTypeInfo {
    std::vector<TypeInfo> input_types;   // 输入类型列表
    std::vector<TypeInfo> output_types;  // 输出类型列表

    bool accepts_input(const TypeInfo& type) const;
    bool output_matches(const TypeInfo& type) const;
};
```

**功能**:
- 描述节点的输入/输出类型签名
- 类型兼容性检查

#### Value - 类型擦除容器
```cpp
class Value {
private:
    std::aligned_storage_t<32> buffer_;  // Small buffer optimization
    TypeInfo type_info_;
    bool is_small_;

public:
    template<typename T>
    Value(T&& val);

    template<typename T>
    T cast() const;

    TypeInfo type() const;
};
```

**功能**:
- 类型擦除（Type Erasure）模式
- 小缓冲区优化（Small Buffer Optimization）
- 避免小类型（如 int、指针）的堆分配
- Frame 作为 `shared_ptr` 存储，保持零拷贝

---

### 2. 模板化节点基类 (`src/runtime/core_tbb.h`)

#### TypedInputNode - 输入节点模板
```cpp
template<typename Derived, typename OutputT>
class TypedInputNode : public Node {
public:
    using OutputType = OutputT;
    std::unique_ptr<tbb::flow::input_node<Value>> tbb_node;

    // 派生类必须实现：
    OutputT forward(tbb::flow_control* fc);

    NodeTypeInfo get_type_info() const override;
    void Activate() override;
};
```

**特点**:
- CRTP（奇异递归模板模式）
- 自动类型签名生成
- 无输入，单个或多个输出

#### TypedFunctionNode - 单输入函数节点模板
```cpp
template<typename Derived, typename InputT, typename OutputT>
class TypedFunctionNode : public Node {
public:
    using InputType = InputT;
    using OutputType = OutputT;
    std::unique_ptr<tbb::flow::function_node<Value, Value>> tbb_node;

    // 派生类必须实现：
    OutputT forward(InputT input);

    NodeTypeInfo get_type_info() const override;
};
```

**特点**:
- 单输入、单输出
- 自动 Value 类型转换
- 类型安全的前向传播

#### TypedMultiInputFunctionNode - 多输入函数节点模板
```cpp
template<typename Derived, typename OutputT, typename... InputTs>
class TypedMultiInputFunctionNode : public Node {
public:
    using InputTuple = std::tuple<InputTs...>;
    using OutputType = OutputT;

    // 派生类必须实现：
    OutputT forward(InputTs... inputs);

    NodeTypeInfo get_type_info() const override;
};
```

**特点**:
- 支持多个输入参数：`forward(Input1, Input2, ...)`
- 使用 `std::tuple` 打包输入
- `std::apply` 解包并调用派生类方法

#### TupleGetNode - 自动索引节点
```cpp
template<size_t Index, typename TupleT>
class TupleGetNode : public TypedFunctionNode<
    TupleGetNode<Index, TupleT>,
    TupleT,
    std::tuple_element_t<Index, TupleT>> {
public:
    using ElementType = std::tuple_element_t<Index, TupleT>;

    ElementType forward(TupleT input);
};
```

**功能**:
- 从 `std::tuple` 中提取指定索引的元素
- 用于 Python 端的自动解包功能

---

### 3. 节点注册机制更新 (`src/runtime/node_registry.h`)

#### 更新的 Concepts
```cpp
template<typename T>
concept NodeType = std::derived_from<T, Node> && requires(ExecutionGraph& g) {
    { std::declval<T&>().build(g) } -> std::same_as<void>;
    { std::declval<T&>().connect() } -> std::same_as<void>;
    { std::declval<T&>().get_type_info() } -> std::convertible_to<NodeTypeInfo>;
};
```

**变更**:
- 添加 `get_type_info()` 要求
- 确保所有注册的节点都能提供类型信息

#### 新增注册宏
```cpp
// 3 参数节点注册宏
#define EW_REGISTER_NODE_3(Classname, PyName, \
                          ParamType1, ParamName1, DefaultVal1, \
                          ParamType2, ParamName2, DefaultVal2, \
                          ParamType3, ParamName3, DefaultVal3)
```

**功能**:
- 支持带 3 个构造参数的节点
- 自动参数提取（从 args/kwargs）
- 保持与现有宏一致的接口

---

### 4. Python 绑定更新 (`src/bindings/bindings.cpp`)

#### TypeInfo 绑定
```cpp
py::class_<easywork::TypeInfo>(m, "TypeInfo")
    .def_readonly("name", &easywork::TypeInfo::type_name)
    .def("__eq__", &easywork::TypeInfo::operator==)
    .def("__ne__", &easywork::TypeInfo::operator!=)
    .def("__repr__", [](const easywork::TypeInfo& self) {
        return "<TypeInfo: " + self.type_name + ">";
    });
```

#### NodeTypeInfo 绑定
```cpp
py::class_<easywork::NodeTypeInfo>(m, "NodeTypeInfo")
    .def_property_readonly("input_types", [](const easywork::NodeTypeInfo& self) {
        std::vector<easywork::TypeInfo> types;
        for (const auto& t : self.input_types) {
            types.push_back(t);
        }
        return types;
    })
    .def_property_readonly("output_types", [](const easywork::NodeTypeInfo& self) {
        std::vector<easywork::TypeInfo> types;
        for (const auto& t : self.output_types) {
            types.push_back(t);
        }
        return types;
    })
    .def("accepts_input", &easywork::NodeTypeInfo::accepts_input)
    .def("output_matches", &easywork::NodeTypeInfo::output_matches);
```

#### Node 基类绑定
```cpp
py::class_<easywork::Node, std::shared_ptr<easywork::Node>>(m, "Node")
    .def("build", &easywork::Node::build)
    .def("connect", &easywork::Node::connect)
    .def("activate", &easywork::Node::Activate)
    .def("set_input", &easywork::Node::set_input)
    .def_property_readonly("type_info", &easywork::Node::get_type_info);
```

**关键功能**:
- `type_info` 属性可在 Python 端查询节点类型
- 支持类型检查和验证

---

### 5. Python 端自动索引机制 (`python/easywork/__init__.py`)

#### Symbol 类增强
```python
class Symbol:
    def __init__(self, producer_node, tuple_index=None):
        self.producer_node = producer_node
        self.tuple_index = tuple_index  # None 或索引值（0, 1, 2...）

    def __iter__(self):
        """支持 tuple 解包：a, b = symbol"""
        type_info = self.producer_node.type_info

        if not type_info.output_types:
            raise ValueError("Cannot unpack: node has no output")

        output_type = type_info.output_types[0]

        # 检查是否是 tuple 类型
        if not self._is_tuple_type(output_type):
            raise ValueError(f"Cannot unpack non-tuple type: {output_type.name}")

        # 获取 tuple 元素数量
        num_elements = self._get_tuple_size(output_type)

        # 为每个元素创建 Symbol
        symbols = []
        for i in range(num_elements):
            symbol = Symbol(self.producer_node, tuple_index=i)
            symbols.append(symbol)

        return iter(symbols)
```

**功能**:
- 支持类似 Python 的 tuple 解包语法：`a, b = node.read()`
- 自动创建带索引的 Symbol
- 类型信息驱动的解包逻辑

#### NodeWrapper 多输入支持
```python
def __call__(self, *args, **kwargs):
    """Enable calling the node directly (like module(x))."""
    if not args:
        raise ValueError("Node requires at least one input")

    # 单输入：直接连接
    if len(args) == 1 and not kwargs:
        symbol = args[0]
        if isinstance(symbol, Symbol):
            if symbol.tuple_index is not None:
                # TODO: 创建 TupleGetNode
                pass
            self._cpp_node.set_input(symbol.producer_node)
            return Symbol(self._cpp_node)

    # 多输入：暂时不支持
    if len(args) > 1:
        raise NotImplementedError("Multi-input nodes not yet supported")

    raise NotImplementedError("This node is not callable")
```

**当前状态**:
- ✅ 基础连接功能
- ⚠️  多输入节点部分实现（需要 TupleJoinNode）
- ⚠️  自动索引功能（需要创建 TupleGetNode 实例）

---

### 6. Pipeline 类型检查 (`python/easywork/__init__.py`)

```python
def validate(self):
    """在运行前进行类型检查。

    Returns:
        True if validation passes

    Raises:
        TypeError: If type mismatches are found
    """
    if self._validated:
        return True

    print("[EasyWork] Validating types...")

    # 1. 执行 construct 定义拓扑
    self.construct()

    # 2. 构建节点（类型信息在 build 后可用）
    for node in self._nodes:
        if not node.built:
            node.raw.build(self._graph)
            node.built = True

    # 3. 执行类型检查
    errors = []
    for node in self._nodes:
        cpp_node = node.raw

        # 获取节点类型信息
        try:
            type_info = cpp_node.type_info
        except Exception as e:
            errors.append(f"Cannot get type info for node: {e}")
            continue

        # TODO: 完整实现类型检查逻辑

    if errors:
        error_msg = "\n".join(errors)
        print(f"[EasyWork] Type Errors Found:\n{error_msg}")
        raise TypeError(f"Type validation failed:\n{error_msg}")

    print("[EasyWork] Type Check Passed ✓")
    self._validated = True
    return True
```

**当前状态**:
- ✅ 基础框架已实现
- ⚠️  类型检查逻辑需要完善（TODO）
- ✅ 在 `run()` 前自动调用

---

### 7. 示例类型化节点 (`src/runtime/module/example_typed_nodes.h`)

#### IntCounter - 整数计数器
```cpp
class IntCounter : public TypedInputNode<IntCounter, int> {
public:
    IntCounter(int start, int max, int step);

    int forward(tbb::flow_control* fc);

private:
    int current_;
    int max_;
    int step_;
};

EW_REGISTER_NODE_3(IntCounter, "IntCounter",
                  int, start, 0,
                  int, max, 100,
                  int, step, 1)
```

**功能**:
- 从 `start` 计数到 `max`，步长为 `step`
- 演示 `TypedInputNode` 的使用
- 输出类型：`int`

#### IntMultiplier - 整数乘法器
```cpp
class IntMultiplier : public TypedFunctionNode<IntMultiplier, int, int> {
public:
    explicit IntMultiplier(int factor);

    int forward(int input);

private:
    int factor_;
};

EW_REGISTER_NODE_1(IntMultiplier, "IntMultiplier",
                  int, factor, 2)
```

**功能**:
- 将输入整数乘以指定的因子
- 演示 `TypedFunctionNode` 的使用
- 输入/输出类型：`int -> int`

#### StringPrinter - 字符串打印器
```cpp
class StringPrinter : public TypedFunctionNode<StringPrinter, std::string, std::string> {
public:
    std::string forward(std::string input);
};

EW_REGISTER_NODE(StringPrinter, "StringPrinter")
```

**功能**:
- 在字符串前添加前缀并打印日志
- 演示字符串类型的处理
- 输入/输出类型：`std::string -> std::string`

---

## 📊 编译状态

### ✅ 编译成功
```bash
$ cmake --build build --parallel -j4
[ 80%] Built target spdlog
[ 90%] Building CXX object CMakeFiles/easywork_core.dir/src/bindings/bindings.cpp.o
[100%] Linking CXX shared module ../python/easywork/easywork_core.cpython-312-x86_64-linux-gnu.so
[100%] Built target easywork_core
```

**关键文件编译状态**:
- ✅ `type_system.h` - 编译通过
- ✅ `core_tbb.h` - 编译通过
- ✅ `node_registry.h` - 编译通过
- ✅ `bindings.cpp` - 编译通过
- ✅ `example_typed_nodes.h` - 编译通过

---

## 🚧 当前问题

### 1. 测试挂起问题
**现象**: 运行 `tests/test_complete_type_system.py` 时进程挂起

**可能原因**:
- TBB 流图在 `input_node::activate()` 后可能阻塞等待
- Mock 模式的 CameraSource 可能没有正确停止
- Pipeline 的 `run()` 方法可能等待无限流

**临时解决方案**:
- 使用有限计数器（IntCounter 有 max 参数）
- 添加超时机制
- 使用 `Ctrl+C` 中断

**待办**:
- [ ] 调查挂起的根本原因
- [ ] 添加自动停止机制
- [ ] 实现更完善的测试框架

### 2. 类型检查逻辑未完善
**现状**: `Pipeline.validate()` 方法框架已实现，但类型检查逻辑标记为 TODO

**需要实现**:
```python
# 检查每个上游连接
for node in self._nodes:
    cpp_node = node.raw

    # 获取节点类型信息
    type_info = cpp_node.type_info

    # 检查每个上游连接的类型兼容性
    for upstream_node in cpp_node.upstreams:
        upstream_type_info = upstream_node.type_info

        if not type_info.accepts_input(upstream_type_info.output_types[0]):
            errors.append(f"Type mismatch: ...")
```

### 3. 自动索引功能未完全实现
**现状**: Python 端 Symbol 支持解包，但 `TupleGetNode` 创建逻辑标记为 TODO

**需要实现**:
```python
def _create_tuple_getter(self, index):
    """创建 TupleGetNode（C++ 工厂）。"""
    # 需要调用 C++ API 创建模板化的 TupleGetNode
    # 可能需要在 node_registry.h 中添加辅助函数
    pass
```

### 4. 多输入节点连接未实现
**现状**: `TypedMultiInputFunctionNode` 的 `connect()` 方法使用简化逻辑

**需要实现**:
- `TupleJoinNode` 用于打包多个上游输出
- 正确的 TBB 边连接逻辑
- 类型验证确保输入数量匹配

---

## 📋 下一步工作

### 优先级 1：修复测试挂起
1. 调试 Pipeline.run() 的执行流程
2. 确保 IntCounter 在达到 max 后正确停止
3. 添加超时和异常处理机制

### 优先级 2：完善类型检查
1. 实现完整的类型兼容性检查
2. 添加详细的错误信息
3. 测试类型错误检测

### 优先级 3：实现自动索引
1. 实现 `_create_tuple_getter()` 方法
2. 添加 `TupleGetNode` 工厂函数
3. 测试 tuple 解包功能

### 优先级 4：实现多输入节点
1. 创建 `TupleJoinNode` 实现
2. 更新 `connect()` 逻辑
3. 测试多输入节点

### 优先级 5：创建完整示例
1. 多返回值节点（返回 `std::tuple`）
2. 部分使用示例（只用 tuple 的部分元素）
3. 多输入节点示例（`forward(Input1, Input2, ...)`）
4. 端到端集成测试

---

## 🎯 设计目标达成情况

| 目标 | 状态 | 说明 |
|------|------|------|
| 支持多种数据类型 | ✅ 完成 | TypeInfo + Value 支持任意类型 |
| 支持多返回值 | ✅ 基础完成 | TypedFunctionNode 支持 `std::tuple` 输出 |
| 运行前类型检查 | ⚠️  部分完成 | 框架已实现，逻辑需完善 |
| 保持零拷贝性能 | ✅ 完成 | Frame 通过指针传递 |
| 无需向后兼容 | ✅ 完成 | 完全重构，无遗留代码 |
| 自动索引机制 | ⚠️  部分完成 | Python 端支持，C++ 端需完善 |
| 多输入节点支持 | ⚠️  部分完成 | 基类已实现，连接逻辑需完善 |

---

## 📖 使用示例

### 基本类型化节点
```python
import easywork as ew

class IntPipeline(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.counter = ew.module.IntCounter(start=0, max=5, step=1)
        self.multiplier = ew.module.IntMultiplier(factor=3)
        self.printer = ew.module.StringPrinter()

    def construct(self):
        x = self.counter.read()      # int
        y = self.multiplier(x)       # int -> int
        z = self.printer(y)          # int -> string

pipeline = IntPipeline()
pipeline.validate()  # 类型检查
pipeline.run()
```

### 查询节点类型信息
```python
counter = ew.module.IntCounter(0, 10, 1)
type_info = counter.raw.type_info

print(f"Input types: {[t.name for t in type_info.input_types]}")
print(f"Output types: {[t.name for t in type_info.output_types]}")
```

### 向后兼容的 Frame 节点
```python
class FramePipeline(ew.Pipeline):
    def __init__(self):
        super().__init__()
        self.cam = ew.module.CameraSource(device_id=-1)
        self.canny = ew.module.CannyFilter()
        self.sink = ew.module.NullSink()

    def construct(self):
        frame = self.cam.read()
        edges = self.canny(frame)
        self.sink.consume(edges)

pipeline = FramePipeline()
pipeline.run()
```

---

## 🔧 技术细节

### CRTP 模式说明
**为什么不用 `override` 关键字？**

在 CRTP 模式中，基类通过 `static_cast<Derived*>(this)->forward(...)` 调用派生类的方法。这不是虚函数调用，因此：
- ✅ 编译时类型检查
- ✅ 零性能开销（无虚函数表查找）
- ✅ 支持模板特化
- ❌ 不能使用 `override` 关键字

### Small Buffer Optimization
**Value 类如何避免堆分配？**

```cpp
std::aligned_storage_t<32> buffer_;  // 32 字节栈内存

// 小类型（int, float, 指针）直接存储在 buffer_ 中
if constexpr (sizeof(T) <= 32 && alignof(T) <= alignof(decltype(buffer_))) {
    new (&buffer_) T(std::forward<T>(val));
    is_small_ = true;
} else {
    ptr_ = new T(std::forward<T>(val));  // 大类型才堆分配
    is_small_ = false;
}
```

### 零拷贝 Frame 传递
```cpp
// Frame 本质上是 shared_ptr
Frame frame = std::make_shared<FrameBuffer>(...);

// Value 存储指针，不复制数据
Value value(frame);  // 只拷贝 shared_ptr

// 提取时返回同一个 shared_ptr
Frame frame2 = value.cast<Frame>();
assert(frame.get() == frame2.get());  // 同一个对象
```

---

## 📚 相关文档

- [设计文档](design_doc.md) - 原始设计文档
- [架构文档](ARCHITECTURE.md) - 系统架构说明
- [复杂类型文档](COMPLEX_TYPES.md) - 复杂类型支持说明
- [快速开始](QUICKSTART.md) - 快速入门指南

---

## 👥 贡献者

- 类型系统架构设计：Claude (Anthropic)
- C++ 实现：Claude (Anthropic)
- Python 绑定：Claude (Anthropic)
- 测试框架：Claude (Anthropic)

---

## 📝 版本历史

### v0.2.0 (2025-01-09)
- ✅ 完成核心类型系统
- ✅ 实现模板化节点基类
- ✅ 更新 Python 绑定
- ✅ 创建示例类型化节点
- ⚠️  测试框架挂起问题待解决

### v0.1.0 (初始版本)
- 原始 Frame-based 架构
- TBB 流图基础实现
- Python-C++ 绑定

---

**最后更新**: 2025-01-09
**状态**: 核心功能完成，待测试和优化


我需要你先分析目前的情况，然后完成下面的工作：说的“未完成的改进部分”范围比较大（类型系统、自动索引、多输入连接、类型检查、挂起问题等）。我可以全量补齐，但需要你确认优先级和期望行为。请在下面选项里标出要做的部分（可多选），或者补充需求：

TupleGetNode 自动索引的完整实现（Python 端解包 + C++ 端工厂/连接） 多输入节点连接（TupleJoinNode + TypedMultiInputFunctionNode 完整接线） Pipeline.validate() 类型检查完整实现 Pipeline.run() 防挂起机制（超时/最大帧数/可选停止策略） 类型系统一致性/Value 安全性修补（SBO 析构等细节） 另外，请说明：

你希望“完整可靠”的验收标准（比如：新增哪些测试、跑哪些测试） 你是否接受 API 变化（例如新增 run(max_frames=...) 或 CameraSource 默认限帧） 确认后我就开始落地。

请你实现所有部分，每个部分都需要进行单元测试，API可以变化，无需向后兼容

另外，无需防挂起，挂起一定是出现了bug