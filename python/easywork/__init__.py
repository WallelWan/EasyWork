from . import easywork_core as _core
import re

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
            raise ValueError(f"Cannot unpack non-tuple type: {output_type.name}")

        # 获取 tuple 元素数量
        num_elements = self._get_tuple_size(output_type)

        # 为每个元素创建 Symbol
        symbols = []
        for i in range(num_elements):
            # 创建自动索引的 Symbol
            symbol = Symbol(self.producer_node, tuple_index=i)
            symbols.append(symbol)

        return iter(symbols)

    def _is_tuple_type(self, type_info):
        """检查类型是否是 tuple。"""
        return "tuple" in type_info.name.lower() or "St4tuple" in type_info.name

    def _get_tuple_size(self, type_info):
        """获取 tuple 的元素数量。"""
        # 从 type_name 解析，例如 "tuple<int, float>" -> 2
        match = re.search(r'tuple<(.+?)>', type_info.name)
        if match:
            types_str = match.group(1)
            # 计算逗号数量 + 1
            return types_str.count(',') + 1
        # 尝试解析 STL tuple 格式
        match = re.search(r'St4tuple<IJ([a-zA-Z0-9_]*)EE', type_info.name)
        if match:
            return int(match.group(1))
        return 1

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
        self._cpp_node.set_input(input_symbol.producer_node)

    def consume(self, input_symbol):
        """Sink node: connect input to this node."""
        self._cpp_node.set_input(input_symbol.producer_node)

    def __call__(self, *args, **kwargs):
        """Enable calling the node directly (like module(x))."""
        if not args:
            raise ValueError("Node requires at least one input")

        # 单输入：直接连接
        if len(args) == 1 and not kwargs:
            symbol = args[0]
            if isinstance(symbol, Symbol):
                # 处理 tuple 索引
                if symbol.tuple_index is not None:
                    # TODO: 创建 TupleGetNode（后续实现）
                    pass
                # 直接连接
                self._cpp_node.set_input(symbol.producer_node)
                return Symbol(self._cpp_node)

        # 多输入：暂时不支持
        if len(args) > 1:
            raise NotImplementedError("Multi-input nodes not yet supported")

        raise NotImplementedError("This node is not callable")

# ========== Dynamic Module (C++20 Factory Pattern) ==========
class _DynamicModule:
    """Dynamic access to C++ registered nodes using factory pattern.

    Allows accessing nodes as: ew.module.CameraSource(), ew.module.CannyFilter(), etc.
    """

    def __init__(self):
        self._cache = {}
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

        return self._cache[name]

    def __dir__(self):
        """Return list of all registered node names for autocompletion."""
        return list(self._registry.registered_nodes())

# Create ew.module access point
module = _DynamicModule()

# ========== PyFunc (Special Case) ==========
class PyFunc(NodeWrapper):
    """Wrapper for Python function nodes (manual binding, not in factory)."""

    def __init__(self, py_callable):
        super().__init__(_core.PyFuncNode(py_callable))

    def call(self, input_symbol):
        """Connect the Python function to the input symbol."""
        self._cpp_node.set_input(input_symbol.producer_node)
        return Symbol(self._cpp_node)

# ========== Pipeline (PyTorch-style) ==========
class Pipeline:
    """PyTorch-style pipeline for defining computation graphs.

    Automatically registers NodeWrapper instances assigned as attributes.
    """

    def __init__(self):
        self._graph = _core.ExecutionGraph()
        self._executor = _core.Executor()
        self._nodes = []
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

            # 检查每个上游连接
            # 注意：暂时简化处理，因为 upstreams 访问可能不可用
            # TODO: 完整实现类型检查逻辑

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
        self.construct()

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
