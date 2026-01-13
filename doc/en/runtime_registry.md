# Runtime Registry System

The Runtime Registry (`src/runtime/registry`) provides the infrastructure for defining, registering, and creating nodes dynamically. It bridges the gap between C++ compile-time templates and Python runtime instantiation.

## 1. Node Registration Macro

The core of the system is the `EW_REGISTER_NODE` macro. It automates the boilerplate required to expose a C++ class to the factory system.

```cpp
EW_REGISTER_NODE(Classname, PyName, Arg("param", default_val)...)
```

- **Classname**: The C++ class name (must inherit from `BaseNode`).
- **PyName**: The string name used in Python (e.g., `ew.module.PyName`).
- **Args**: Optional definitions for constructor arguments.

### How it works
The macro instantiates a static `NodeRegistrar` object. The constructor of this object registers a lambda function (the `NodeCreator`) into the global `NodeRegistry`.

## 2. Node Registry Singleton

`NodeRegistry` is a singleton class that maintains a map of factory functions:
`std::unordered_map<std::string, NodeCreator> creators_;`

### NodeCreator
The `NodeCreator` is a function with the signature:
```cpp
using NodeCreator = std::function<std::shared_ptr<Node>(
    pybind11::args, pybind11::kwargs)>;
```
It handles the parsing of Python `args` and `kwargs` and invokes the C++ constructor.

## 3. Reflection Macros

To support the heterogeneous method system, nodes must export their methods using the `EW_ENABLE_METHODS` macro.

```cpp
class MyNode : public BaseNode<MyNode> {
    int forward(int x) { ... }
    EW_ENABLE_METHODS(forward)
};
```

This macro generates:
1.  **Method Registry**: A static map containing `MethodMeta` (invokers, argument types, return types) for each exported method.
2.  **Exposed Methods List**: A list of method names for Python introspection.

## 4. Factory Pattern & Argument Extraction

The registration system supports robust argument parsing for node constructors.

- **Arg Struct**: `Arg("name", value)` defines a parameter name and a default value.
- **Template Metaprogramming**: `CreateNodeWithArgs` unpacks the variadic `Arg` definitions.
- **Fallback Strategy**:
    1.  Checks positional arguments (`args`).
    2.  Checks keyword arguments (`kwargs`).
    3.  Falls back to the default value provided in `Arg`.

This allows Python users to instantiate nodes flexibly:
```python
# All valid if defined with Arg("factor", 1)
node = MyNode(2)
node = MyNode(factor=2)
node = MyNode() # uses default 1
```
