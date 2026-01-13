# 运行时类型系统 (Runtime Type System)

运行时类型系统 (`src/runtime/types`) 为动态执行图中的类型安全、自动转换和 Python 集成提供了稳健的机制。

## 1. TypeInfo

`TypeInfo` 是 EasyWork 中类型的核心描述符。它是标准 C++ RTTI (`std::type_info`) 的轻量级包装器，但增加了以下能力：
- **存储**：使用 `std::type_index` 以便用作 Map 的键。
- **Demangling**：提供人类可读的类型名称（例如 `std::vector<int>` 而不是 `St6vectorIiE`）。
- **比较**：支持相等性检查以验证连接兼容性。

## 2. 类型转换注册表 (Type Converter Registry)

EasyWork 支持节点间的自动类型转换（例如，将 `int` 输出连接到 `double` 输入）。

### 2.1 TypeConverterRegistry
存储转换函数的单例注册表：
`using TypeConverter = std::function<std::any(const std::any&)>;`

### 2.2 自动算术注册
系统通过 `RegisterArithmeticConversions` 和 `AutoRegistrar` 模板自动注册常见的算术提升（`int` -> `float`, `float` -> `double` 等）。

### 2.3 编译时 Traits
`src/runtime/types/type_traits.h` 定义了 `Converter<From, To>` 结构体，在编译时确定是否可以进行安全转换，默认防止不安全的转换（如 `double` -> `int` 的有损转换）。

## 3. Packet 与 std::any

`Packet` 类（定义在 `core.h` 中，但严重依赖类型系统）使用 `std::any` 存储数据。
- **类型擦除**：允许图处理任何 C++ 类型，而无需模板感染核心引擎。
- **验证**：`Packet::cast<T>()` 在转换前验证存储的 `typeid` 是否与请求的 `T` 匹配，不匹配时抛出清晰的错误。

## 4. Python 集成 (AnyToPy)

为了支持 Python 交互，系统维护了从 C++ 类型到 Pybind11 转换器的映射。

- **AnyToPyRegistry**：将 `std::type_index` 映射到将 `std::any` 转换为 `pybind11::object` 的函数。
- **注册**：当使用 `EW_REGISTER_NODE` 时，节点的参数和返回类型会通过 `RegisterPythonType<T>()` 自动注册到此系统中。
- **使用**：当节点结果返回给 Python（在 Eager 模式下）时，框架在此注册表中查找类型，将 `std::any` 载荷转换为原生 Python 对象（例如 `std::vector` -> `list`）。
