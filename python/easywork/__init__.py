from . import easywork_core as _core

_ACTIVE_PIPELINE = None

def hash_string(s):
    # Simple hash to match C++ (fnv1a) or use _core.hash_string if exposed?
    # bindings.cpp didn't expose hash_string directly, but exposed ID_FORWARD which is hashed.
    # Actually, NodeWrapper needs to hash method names to look up in type_info.
    # We can compute it or rely on looking up by name if type_info keys were names?
    # type_info.methods keys are size_t (hashes).
    # So we NEED the hash function.
    # Helper: _core.hash_string is NOT exposed in bindings.cpp shown earlier.
    # But wait, bindings.cpp:
    # m.attr("ID_FORWARD") = easywork::ID_FORWARD;
    # It does NOT expose hash_string.
    # I should have exposed it.
    
    # Workaround: Iterate type_info.methods (which are hashes) 
    # But how do we know which hash corresponds to "set_string"?
    # We don't.
    # This is a problem.
    # However, exposed_methods() returns names.
    # We can build a local map {name: hash} if we can't compute hash.
    # BUT we can't compute hash without the function.
    # Wait, easywork::hash_string is constexpr. 
    # I can implement FNV-1a in Python.
    
    hash_val = 14695981039346656037
    for char in s:
        hash_val ^= ord(char)
        hash_val *= 1099511628211
        hash_val &= 0xFFFFFFFFFFFFFFFF # Force 64-bit
    return hash_val

# ========== Symbol ==========
class Symbol:
    """Represents a data flow connection between nodes."""
    def __init__(self, producer_node, source_method_id=None, tuple_index=None):
        self.producer_node = producer_node
        # Default to forward if not specified
        self.source_method_id = source_method_id if source_method_id is not None else _core.ID_FORWARD
        self.tuple_index = tuple_index

    def __iter__(self):
        """Support tuple unpacking: a, b = symbol"""
        type_info = self.producer_node.type_info

        if self.source_method_id not in type_info.methods:
             raise ValueError(f"Unknown source method ID {self.source_method_id}")

        output_type = type_info.methods[self.source_method_id].output_type

        # Check tuple
        if not self._is_tuple_type(output_type):
            if "tuple" in output_type.name.lower() or "St4tuple" in output_type.name:
                 raise ValueError(f"Tuple type not registered: {output_type.name}")
            raise ValueError(f"Cannot unpack non-tuple type: {output_type.name}")

        num_elements = self._get_tuple_size(output_type)
        if num_elements <= 0:
            raise ValueError(f"Tuple type not registered: {output_type.name}")

        symbols = []
        for i in range(num_elements):
            # Create symbol sharing the same source method, but with tuple index
            symbol = Symbol(self.producer_node, self.source_method_id, tuple_index=i)
            symbols.append(symbol)

        return iter(symbols)

    def _is_tuple_type(self, type_info):
        return _core.get_tuple_size(type_info) > 0

    def _get_tuple_size(self, type_info):
        return _core.get_tuple_size(type_info)

# ========== Node Wrapper ==========
class NodeWrapper:
    def __init__(self, raw_node, pipeline=None):
        self.raw = raw_node
        self.pipeline = pipeline
        self.built = False
        
        # Build method name to ID map
        self._method_name_to_id = {}
        self._id_to_method_name = {}
        # We can't easily map names to IDs without the hash function or the list.
        # exposed_methods gives us names.
        # We can try hashing them to see if they match keys in type_info.methods.
        for name in self.raw.exposed_methods:
             h = hash_string(name)
             self._method_name_to_id[name] = h
             self._id_to_method_name[h] = name
        
        # Ensure ID_FORWARD is always mapped
        if _core.ID_FORWARD not in self._id_to_method_name:
             self._id_to_method_name[_core.ID_FORWARD] = "forward"

    @property
    def type_name(self):
        # Try to get type name from raw node if we add it to C++
        # For now, use the task name if available or fallback
        try:
            return self.raw.type_name
        except AttributeError:
            return "Node"

    def __str__(self):
        return f"{self.type_name}"

    @property
    def type_info(self):
        return self.raw.type_info

    @property
    def is_open(self):
        return self.raw.is_open()

    def open(self, *args, **kwargs):
        return self.raw.open(*args, **kwargs)

    def close(self, *args, **kwargs):
        return self.raw.close(*args, **kwargs)

    def read(self):
        # Legacy support for Source nodes
        return self.__call__()

    def __getattr__(self, name):
        # Check if it is a method
        if name in self._method_name_to_id:
            method_id = self._method_name_to_id[name]
            return self._create_method_proxy(name, method_id)
        
        # Fallback to raw (but raw is C++ object, attributes might not work directly as expected)
        # But bindings expose some.
        return getattr(self.raw, name)

    def _create_method_proxy(self, name, method_id):
        def proxy(*args, **kwargs):
            return self._connect(name, method_id, *args, **kwargs)
        return proxy

    def __call__(self, *args, **kwargs):
        # Default connection to 'forward'
        return self._connect("forward", _core.ID_FORWARD, *args, **kwargs)

    def _connect(self, method_name, method_id, *args, **kwargs):
        # Eager Mode: If no active pipeline, execute immediately
        if _ACTIVE_PIPELINE is None:
             return self.raw.invoke(method_name, *args)

        # Tracing Mode
        if self not in _ACTIVE_PIPELINE._nodes:
            _ACTIVE_PIPELINE._nodes.append(self)
        
        # Process inputs
        for idx, arg in enumerate(args):
            upstream_node = None
            upstream_method_id = _core.ID_FORWARD
            
            if isinstance(arg, Symbol):
                upstream_node = arg.producer_node
                upstream_method_id = arg.source_method_id
                
                if arg.tuple_index is not None:
                     # Handle Tuple extraction
                     # Create a temporary TupleGetNode
                     # This is complex to do on the fly without a factory exposed nicely.
                     # But core.create_tuple_get_node exists.
                     
                     # Get output type of upstream
                     up_type = upstream_node.type_info.methods[upstream_method_id].output_type
                     
                     tuple_node_ptr = _core.create_tuple_get_node(up_type, arg.tuple_index)
                     if _ACTIVE_PIPELINE:
                         tuple_wrapper = _ACTIVE_PIPELINE._register_internal_node(tuple_node_ptr)
                         
                         # Connect Upstream -> TupleGet
                         # TupleGetNode has 'forward' (ID_FORWARD) input
                         tuple_wrapper.raw.set_input_for("forward", upstream_node)
                         
                         # Track connection for TupleGet
                         # Upstream (method_id) -> TupleGet (forward)
                         _ACTIVE_PIPELINE._record_connection(
                             tuple_wrapper.raw, _core.ID_FORWARD, 0,
                             upstream_node, upstream_method_id
                         )
                         
                         upstream_node = tuple_wrapper.raw
                         upstream_method_id = _core.ID_FORWARD
                     else:
                         raise RuntimeError("Tuple unpacking requires active pipeline context")

            elif isinstance(arg, NodeWrapper):
                # Ensure upstream node is registered in the pipeline
                if _ACTIVE_PIPELINE and arg not in _ACTIVE_PIPELINE._nodes:
                    _ACTIVE_PIPELINE._nodes.append(arg)
                
                upstream_node = arg.raw
                upstream_method_id = _core.ID_FORWARD # Assume forward default
            
            # TODO: Handle literals (ConstantNode) 
            
            if upstream_node:
                self.raw.set_input_for(method_name, upstream_node)
                if _ACTIVE_PIPELINE:
                    _ACTIVE_PIPELINE._record_connection(
                        self.raw, method_id, idx,
                        upstream_node, upstream_method_id
                    )

        # Return output symbol
        return Symbol(self.raw, source_method_id=method_id)


# ========== Pipeline ==========
class Pipeline:
    def __init__(self):
        self._graph = _core.ExecutionGraph()
        self._executor = _core.Executor()
        self._nodes = []
        self._internal_nodes = []
        self._validated = False
        self._has_run = False
        # Metadata: {(node_ptr, method_id, input_idx): (upstream_node_ptr, upstream_method_id)}
        self._connection_metadata = {}
    
    def __setattr__(self, name, value):
        # Auto-register NodeWrappers assigned to the pipeline (Torch-like style)
        if isinstance(value, NodeWrapper):
            if hasattr(self, "_nodes") and value not in self._nodes:
                self._nodes.append(value)
        super().__setattr__(name, value)
    
    def __enter__(self):
        global _ACTIVE_PIPELINE
        self._previous_pipeline = _ACTIVE_PIPELINE
        _ACTIVE_PIPELINE = self
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        global _ACTIVE_PIPELINE
        _ACTIVE_PIPELINE = self._previous_pipeline

    def _record_connection(self, consumer, consumer_method, input_idx, producer, producer_method):
        # Use python id() or raw pointer address if available?
        # raw is shared_ptr, so it's unique per node instance.
        # We can use the object itself as key if it's hashable, or id().
        key = (consumer, consumer_method, input_idx)
        val = (producer, producer_method)
        self._connection_metadata[key] = val

    def construct(self):
        pass

    def _clear_topology(self):
        """Clears all topology information to allow reconstruction."""
        # 1. Reset C++ connections for existing nodes
        # (Needed for Torch-like style where nodes persist)
        self._reset_connections()
        
        # 2. Clear Python node list
        # (Needed for Construct-style where new nodes are created)
        self._nodes.clear()
        
        # 3. Clear internals
        self._clear_internal_nodes()
        self._connection_metadata.clear()

    def _build_topology(self):
        """Reconstructs the topology by clearing and running user code."""
        # Heuristic: If nodes exist and construct is default, assume 'with pipeline:' usage
        # and do NOT clear topology.
        is_default_construct = self.construct.__func__ is Pipeline.construct
        if self._nodes and is_default_construct:
            print("[EasyWork] Detected external graph construction, preserving nodes...")
            return

        self._clear_topology()
        self._with_active_pipeline(self.construct)

    def validate(self):
        print("[EasyWork] Validating types...")
        self._build_topology()

        errors = []
        wrapper_by_raw = {node.raw: node for node in self._nodes}
        for node in self._nodes:
            cpp_node = node.raw
            type_info = cpp_node.type_info
            
            # Reconstruct mapping:
            method_arg_counters = {} # method_id -> current_arg_idx
            
            # Iterate C++ connections to sync with metadata
            for i, conn in enumerate(cpp_node.connections):
                if not conn.node: continue
                
                mid = conn.method_id
                arg_idx = method_arg_counters.get(mid, 0)
                method_arg_counters[mid] = arg_idx + 1
                
                # Retrieve metadata
                meta_key = (cpp_node, mid, arg_idx)
                
                upstream_method_id = _core.ID_FORWARD
                if meta_key in self._connection_metadata:
                    _, upstream_method_id = self._connection_metadata[meta_key]
                else:
                    # Fallback or error?
                    pass
                
                # Check types
                method_name = node._id_to_method_name.get(mid, str(mid))
                if mid not in type_info.methods:
                    errors.append(f"Node {node} inputs to unknown method {method_name}")
                    continue
                
                input_types = type_info.methods[mid].input_types
                if arg_idx >= len(input_types):
                     errors.append(f"Too many inputs for method {method_name}")
                     continue
                
                expected_type = input_types[arg_idx]
                
                # Upstream output type
                up_node = conn.node
                up_type_info = up_node.type_info
                up_wrapper = wrapper_by_raw.get(up_node)
                upstream_method_name = str(upstream_method_id)
                if up_wrapper:
                    upstream_method_name = up_wrapper._id_to_method_name.get(
                        upstream_method_id,
                        upstream_method_name,
                    )
                
                if upstream_method_id not in up_type_info.methods:
                     errors.append(f"Upstream method {upstream_method_name} not found")
                     continue
                
                actual_type = up_type_info.methods[upstream_method_id].output_type
                
                if expected_type != actual_type:
                    errors.append(
                        f"Type mismatch: Node {node} Method {method_name} Arg {arg_idx} "
                        f"expects {expected_type.name}, got {actual_type.name} "
                        f"(from {up_node} method {upstream_method_name})")

        if errors:
            raise TypeError("\n".join(errors))

        self._validated = True
        return True

    def open(self):
        for node in self._nodes:
            node.open()

    def close(self):
        for node in self._nodes:
            node.close()

    def _ensure_all_open(self):
        internal_nodes = set(getattr(self, "_internal_nodes", []))
        not_open = [node for node in self._nodes
                    if node not in internal_nodes and not node.is_open]
        if not_open:
            names = ", ".join(str(n) for n in not_open)
            raise RuntimeError(f"All nodes must be opened before run(). Closed nodes: {names}")

    def run(self):
        # If not validated/constructed recently, build topology now.
        if not self._validated:
            self._build_topology()

        if self._has_run:
            print("[EasyWork] Resetting Graph for re-run...")
            self._graph.reset()
            for node in self._nodes:
                node.built = False

        print(f"[EasyWork] Materializing Graph ({len(self._nodes)} nodes)...")
        for node in self._nodes:
            if not node.built:
                node.raw.build(self._graph)
                node.built = True

        print("[EasyWork] Connecting Edges...")
        for node in self._nodes:
            node.raw.connect()

        for node in self._nodes:
            node.raw.activate()

        self._ensure_all_open()
        print("[EasyWork] Starting Executor...")
        try:
            self._executor.run(self._graph)
        except KeyboardInterrupt:
            print("\n[EasyWork] Stopping...")
        self._has_run = True
        
        # Invalidate after run to force rebuild next time unless re-validated.
        # This supports dynamic changes in 'construct' between runs.
        self._validated = False

    def _with_active_pipeline(self, fn):
        global _ACTIVE_PIPELINE
        previous = _ACTIVE_PIPELINE
        _ACTIVE_PIPELINE = self
        try:
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

# Module proxy

class ModuleProxy:
    def __getattr__(self, name):
        if _core._NodeRegistry.instance().is_registered(name):
            # Return a factory function
            def factory(*args, **kwargs):
                node = _core.create_node(name, *args, **kwargs)
                return NodeWrapper(node)
            return factory
        raise AttributeError(f"Node type '{name}' not found")

    def __dir__(self):
        return _core._NodeRegistry.instance().registered_nodes()



module = ModuleProxy()
