# Runtime Type System

The Runtime Type System (`src/runtime/types`) provides a robust mechanism for type safety, automatic conversion, and Python integration within the dynamic execution graph.

## 1. TypeInfo

`TypeInfo` is the core descriptor for types in EasyWork. It acts as a lightweight wrapper around standard C++ RTTI (`std::type_info`) but adds capabilities for:
- **Storage**: Uses `std::type_index` for use as map keys.
- **Demangling**: Provides human-readable type names (e.g., `std::vector<int>` instead of `St6vectorIiE`).
- **Comparison**: Supports equality checks to validate connection compatibility.

## 2. Type Converter Registry

EasyWork supports automatic type conversion between nodes (e.g., connecting an `int` output to a `double` input).

### 2.1 TypeConverterRegistry
A singleton registry that stores conversion functions:
`using TypeConverter = std::function<std::any(const std::any&)>;`

### 2.2 Automatic Arithmetic Registration
The system automatically registers common arithmetic promotions (`int` -> `float`, `float` -> `double`, etc.) via `RegisterArithmeticConversions` and the `AutoRegistrar` template.

### 2.3 Compile-Time Traits
`src/runtime/types/type_traits.h` defines `Converter<From, To>` structs that determine at compile-time if a safe conversion is possible, preventing unsafe casts (like `double` -> `int` lossy conversion) by default.

## 3. Packet & std::any

The `Packet` class (defined in `core.h` but heavily relying on the type system) uses `std::any` to store data.
- **Type Erasure**: Allows the graph to handle any C++ type without templates infecting the core engine.
- **Validation**: `Packet::cast<T>()` verifies the stored `typeid` against the requested `T` before casting, throwing a clear error on mismatch.

## 4. Python Integration (AnyToPy)

To support Python interaction, the system maintains a mapping from C++ types to Pybind11 converters.

- **AnyToPyRegistry**: Maps `std::type_index` to a function that converts `std::any` -> `pybind11::object`.
- **Registration**: When `EW_REGISTER_NODE` is used, the arguments and return types of the node are automatically registered into this system using `RegisterPythonType<T>()`.
- **Usage**: When a node result is returned to Python (in Eager mode), the framework looks up the type in this registry to convert the `std::any` payload into a native Python object (e.g., `std::vector` -> `list`).
