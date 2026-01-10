from . import easywork_core as _core

_ACTIVE_PIPELINE = None
_SYMBOLIC_MODE = 0

# ========== Symbol ==========
class Symbol:
    """Represents a data flow connection between nodes."""
    def __init__(self, producer_node, tuple_index=None):
        self.producer_node = producer_node
        self.tuple_index = tuple_index  # None 或索引值（0, 1, 2...）

    def __iter__(self):
        """支持 tuple 解包：a, b = symbol"""
        # 获取上游节点的输出类型
        type_info = self.producer_node.type_info

        if not type_info.output_types:
            raise ValueError("Cannot unpack: node has no output")

        output_type = type_info.output_types[0]

        # 检查是否是 tuple 类型
        if not self._is_tuple_type(output_type):
            if "tuple" in output_type.name.lower() or "St4tuple" in output_type.name:
                raise ValueError(f"Tuple type not registered: {output_type.name}")
            raise ValueError(f"Cannot unpack non-tuple type: {output_type.name}")

        # 获取 tuple 元素数量
        num_elements = self._get_tuple_size(output_type)
        if num_elements <= 0:
            raise ValueError(f"Tuple type not registered: {output_type.name}")

        # 为每个元素创建 Symbol
        symbols = []
        for i in range(num_elements):
            # 创建自动索引的 Symbol
            symbol = Symbol(self.producer_node, tuple_index=i)
            symbols.append(symbol)

        return iter(symbols)

    def _is_tuple_type(self, type_info):
        """检查类型是否是 tuple。"""
        return _core.get_tuple_size(type_info) > 0

    def _get_tuple_size(self, type_info):
        """获取 tuple 的元素数量。"""
        return _core.get_tuple_size(type_info)

# ========== Base Node Wrapper ==========
class NodeWrapper:
    """Base class for all Python node wrappers."""
    def __init__(self, cpp_node):
        self._cpp_node = cpp_node
        self.built = False

    @property
    def raw(self):
        """Access the underlying C++ node."""
        return self._cpp_node

    def read(self):
        """Source node: create output symbol."""
        return Symbol(self._cpp_node)

    def write(self, input_symbol):
        """Sink node: connect input to this node."""
        symbol = _resolve_symbol(input_symbol)
        self._cpp_node.set_input(symbol.producer_node)

    def consume(self, input_symbol):
        """Sink node: connect input to this node."""
        symbol = _resolve_symbol(input_symbol)
        self._cpp_node.set_input(symbol.producer_node)

    def _call_method(self, method_name, *args, **kwargs):
        """Call a named method on the node."""
        if not args:
            raise ValueError("Node requires at least one input")

        if kwargs:
            raise NotImplementedError("Keyword inputs are not supported")

        if len(args) == 1:
            symbol = _resolve_symbol(args[0])
            self._cpp_node.set_input_for(method_name, symbol.producer_node)
            return Symbol(self._cpp_node)

        if len(args) > 1:
            for symbol in args:
                resolved = _resolve_symbol(symbol)
                self._cpp_node.set_input_for(method_name, resolved.producer_node)
            return Symbol(self._cpp_node)

        raise NotImplementedError("This node is not callable")

    def __call__(self, *args, **kwargs):
        """Enable calling the node directly (like module(x))."""
        return self._call_method("forward", *args, **kwargs)

    def __getattr__(self, name):
        exposed_methods = self._cpp_node.exposed_methods
        if name in exposed_methods:
            def _method(*args, **kwargs):
                return self._call_method(name, *args, **kwargs)
            return _method
        raise AttributeError(f"'{self.__class__.__name__}' has no attribute '{name}'")

    def __dir__(self):
        return sorted(set(super().__dir__()) | set(self._cpp_node.exposed_methods))

def _get_active_pipeline():
    if _ACTIVE_PIPELINE is None:
        raise RuntimeError("Tuple indexing requires an active Pipeline context")
    return _ACTIVE_PIPELINE


def _register_internal_node(cpp_node):
    pipeline = _get_active_pipeline()
    return pipeline._register_internal_node(cpp_node)


def _resolve_symbol(symbol):
    if not isinstance(symbol, Symbol):
        raise TypeError("Input must be a Symbol")
    if symbol.tuple_index is None:
        return symbol
    type_info = symbol.producer_node.type_info
    if not type_info.output_types:
        raise ValueError("Tuple producer has no outputs")
    tuple_get_node = _core.create_tuple_get_node(type_info.output_types[0],
                                                 symbol.tuple_index)
    wrapper = _register_internal_node(tuple_get_node)
    wrapper.raw.set_input(symbol.producer_node)
    return Symbol(wrapper.raw)


def _is_symbolic_mode():
    return _SYMBOLIC_MODE > 0 or _ACTIVE_PIPELINE is not None


class _SymbolicScope:
    def __enter__(self):
        global _SYMBOLIC_MODE
        _SYMBOLIC_MODE += 1
        return self

    def __exit__(self, exc_type, exc, tb):
        global _SYMBOLIC_MODE
        _SYMBOLIC_MODE -= 1


def symbolic_context(fn):
    """Decorator to run a function with symbolic ew.module behavior."""
    def _wrapped(*args, **kwargs):
        with _SymbolicScope():
            return fn(*args, **kwargs)
    return _wrapped

# ========== Dynamic Module (C++20 Factory Pattern) ==========
class _DynamicModule:
    """Dynamic access to C++ registered nodes using factory pattern.

    Allows accessing nodes as: ew.module.NumberSource(), ew.module.MultiplyBy(), etc.
    """

    def __init__(self):
        self._cache = {}
        self._factory_cache = {}
        self._registry = _core._NodeRegistry.instance()

    def __getattr__(self, name):
        """Lazily create and cache Python wrapper classes for C++ nodes."""
        if name not in self._cache:
            if not self._registry.is_registered(name):
                available = self._registry.registered_nodes()
                raise AttributeError(
                    f"'{self.__class__.__name__}' has no attribute '{name}'. "
                    f"Available nodes: {available}"
                )

            # Create dynamic wrapper class on first access
            def __init__(self, *args, **kwargs):
                self._cpp_node = _core.create_node(name, *args, **kwargs)
                self.built = False

            cls = type(name, (NodeWrapper,), {
                '__init__': __init__,
                '__module__': 'easywork.module',
                '__doc__': f'Dynamically generated wrapper for {name} (C++20 factory pattern)',
            })
            self._cache[name] = cls

        if _is_symbolic_mode():
            return self._cache[name]

        if name not in self._factory_cache:
            def _factory(*args, **kwargs):
                return _core.create_node(name, *args, **kwargs)
            _factory.__name__ = name
            _factory.__qualname__ = name
            _factory.__doc__ = f'Create a {name} node instance.'
            self._factory_cache[name] = _factory

        return self._factory_cache[name]

    def __dir__(self):
        """Return list of all registered node names for autocompletion."""
        return list(self._registry.registered_nodes())

# Create ew.module access point
module = _DynamicModule()

# ========== Pipeline (PyTorch-style) ==========
class Pipeline:
    """PyTorch-style pipeline for defining computation graphs.

    Automatically registers NodeWrapper instances assigned as attributes.
    """

    def __init__(self):
        self._graph = _core.ExecutionGraph()
        self._executor = _core.Executor()
        self._nodes = []
        self._internal_nodes = []
        self._has_run = False
        self._validated = False

    def __setattr__(self, name, value):
        """Magic: Automatically register NodeWrapper instances assigned to self."""
        # Standard assignment first
        object.__setattr__(self, name, value)

        # Registration logic
        if isinstance(value, NodeWrapper):
            if not hasattr(self, '_nodes'):
                self._nodes = []
            if value not in self._nodes:
                self._nodes.append(value)

    def construct(self):
        """User should override this to define connections (topology)."""
        raise NotImplementedError("You must implement construct() method")

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
        self._clear_internal_nodes()
        self._reset_connections()
        self._with_active_pipeline(self.construct)

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

            upstreams = list(cpp_node.upstreams)

            if not type_info.input_types:
                if upstreams:
                    errors.append(
                        f"Type mismatch: source node has unexpected inputs "
                        f"(got {len(upstreams)})")
                continue

            if len(upstreams) != len(type_info.input_types):
                errors.append(
                    f"Type mismatch: node expects {len(type_info.input_types)} inputs "
                    f"but got {len(upstreams)}")
                continue

            for idx, upstream in enumerate(upstreams):
                upstream_type = upstream.type_info
                if not upstream_type.output_types:
                    errors.append(
                        f"Type mismatch: upstream node has no outputs at input {idx}")
                    continue
                if len(upstream_type.output_types) != 1:
                    errors.append(
                        f"Type mismatch: upstream node has multiple outputs at input {idx}")
                    continue
                if upstream_type.output_types[0] != type_info.input_types[idx]:
                    errors.append(
                        f"Type mismatch: expected {type_info.input_types[idx].name} "
                        f"but got {upstream_type.output_types[0].name} at input {idx}")

        if errors:
            error_msg = "\n".join(errors)
            print(f"[EasyWork] Type Errors Found:\n{error_msg}")
            raise TypeError(f"Type validation failed:\n{error_msg}")

        print("[EasyWork] Type Check Passed ✓")
        self._validated = True
        return True

    def run(self):
        """Execute the pipeline: trace → build → connect → execute."""
        if not self._validated:
            self.validate()

        print("[EasyWork] Tracing topology...")
        if self._has_run:
            self._graph.reset()

        # 1. Trace: Execute user's connection logic
        self._clear_internal_nodes()
        self._reset_connections()
        self._with_active_pipeline(self.construct)

        # 2. Build: Create all C++ TBB nodes
        print(f"[EasyWork] Materializing Graph ({len(self._nodes)} nodes)...")
        for node in self._nodes:
            if not node.built:
                node.raw.build(self._graph)
                node.built = True

        # 3. Connect: Link TBB edges
        print("[EasyWork] Connecting Edges...")
        for node in self._nodes:
            node.raw.connect()

        # 3.5. Activate sources after edges are wired
        for node in self._nodes:
            node.raw.activate()

        # 4. Execute
        print("[EasyWork] Starting Executor...")
        try:
            self._executor.run(self._graph)
        except KeyboardInterrupt:
            print("\n[EasyWork] Stopping...")
        self._has_run = True

    def _with_active_pipeline(self, fn):
        global _ACTIVE_PIPELINE
        previous = _ACTIVE_PIPELINE
        _ACTIVE_PIPELINE = self
        try:
            with _SymbolicScope():
                return fn()
        finally:
            _ACTIVE_PIPELINE = previous

    def _register_internal_node(self, cpp_node):
        wrapper = NodeWrapper(cpp_node)
        self._internal_nodes.append(wrapper)
        if wrapper not in self._nodes:
            self._nodes.append(wrapper)
        return wrapper

    def _clear_internal_nodes(self):
        if not hasattr(self, "_internal_nodes"):
            self._internal_nodes = []
        if not self._internal_nodes:
            return
        for node in self._internal_nodes:
            if node in self._nodes:
                self._nodes.remove(node)
        self._internal_nodes = []

    def _reset_connections(self):
        for node in self._nodes:
            node.raw.clear_upstreams()
