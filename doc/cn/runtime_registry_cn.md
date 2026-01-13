# 运行时注册系统 (Runtime Registry System)

运行时注册系统 (`src/runtime/registry`) 提供了动态定义、注册和创建节点的基础设施。它弥合了 C++ 编译时模板与 Python 运行时实例化之间的鸿沟。

## 1. 节点注册宏 (Node Registration Macro)

系统的核心是 `EW_REGISTER_NODE` 宏。它自动化了将 C++ 类暴露给工厂系统所需的样板代码。

```cpp
EW_REGISTER_NODE(Classname, PyName, Arg("param", default_val)...)
```

- **Classname**：C++ 类名（必须继承自 `BaseNode`）。
- **PyName**：Python 中使用的字符串名称（例如 `ew.module.PyName`）。
- **Args**：构造函数参数的可选定义。

### 工作原理
该宏实例化一个静态的 `NodeRegistrar` 对象。此对象的构造函数将一个 lambda 函数（`NodeCreator`）注册到全局 `NodeRegistry` 中。

## 2. 节点注册单例 (Node Registry Singleton)

`NodeRegistry` 是一个单例类，维护着工厂函数的映射：
`std::unordered_map<std::string, NodeCreator> creators_;`

### NodeCreator
`NodeCreator` 是一个具有以下签名的函数：
```cpp
using NodeCreator = std::function<std::shared_ptr<Node>(
    pybind11::args, pybind11::kwargs)>;
```
它处理 Python `args` 和 `kwargs` 的解析，并调用 C++ 构造函数。

## 3. 反射宏 (Reflection Macros)

为了支持异构方法系统，节点必须使用 `EW_ENABLE_METHODS` 宏导出其方法。

```cpp
class MyNode : public BaseNode<MyNode> {
    int forward(int x) { ... }
    EW_ENABLE_METHODS(forward)
};
```

此宏生成：
1.  **方法注册表 (Method Registry)**：一个包含每个导出方法的 `MethodMeta`（调用器、参数类型、返回类型）的静态映射。
2.  **暴露方法列表 (Exposed Methods List)**：用于 Python 内省的方法名称列表。

## 4. 工厂模式与参数提取

注册系统支持稳健的节点构造函数参数解析。

- **Arg 结构体**：`Arg("name", value)` 定义参数名称和默认值。
- **模板元编程**：`CreateNodeWithArgs` 解包变长 `Arg` 定义。
- **回退策略**：
    1.  检查位置参数 (`args`)。
    2.  检查关键字参数 (`kwargs`)。
    3.  回退到 `Arg` 中提供的默认值。

这允许 Python 用户灵活地实例化节点：
```python
# 如果定义了 Arg("factor", 1)，以下均有效：
node = MyNode(2)
node = MyNode(factor=2)
node = MyNode() # 使用默认值 1
```
