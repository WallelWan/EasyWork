import json
import logging
import types
from datetime import datetime

from . import easywork_core as _core

_ACTIVE_PIPELINE = None
_ACTIVE_BRANCH_CONTEXTS = []
_LOG = logging.getLogger("easywork")

def hash_string(s):
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
        self.source_method_id = source_method_id if source_method_id is not None else _core.ID_FORWARD
        self.tuple_index = tuple_index

    def __iter__(self):
        type_info = self.producer_node.type_info

        if self.source_method_id not in type_info.methods:
             raise ValueError(f"Unknown source method ID {self.source_method_id}")

        output_type = type_info.methods[self.source_method_id].output_type

        if not self._is_tuple_type(output_type):
            if "tuple" in output_type.name.lower() or "St4tuple" in output_type.name:
                 raise ValueError(f"Tuple type not registered: {output_type.name}")
            raise ValueError(f"Cannot unpack non-tuple type: {output_type.name}")

        num_elements = self._get_tuple_size(output_type)
        if num_elements <= 0:
            raise ValueError(f"Tuple type not registered: {output_type.name}")

        symbols = []
        for i in range(num_elements):
            symbol = Symbol(self.producer_node, self.source_method_id, tuple_index=i)
            symbols.append(symbol)

        return iter(symbols)

    def _is_tuple_type(self, type_info):
        return _core.get_tuple_size(type_info) > 0

    def _get_tuple_size(self, type_info):
        return _core.get_tuple_size(type_info)

class MuxSymbol:
    """Represents a conditional data flow (multiplexed)."""
    def __init__(self, control_node, mapping):
        self.control_node = control_node
        self.mapping = mapping

class _BranchScope:
    def __init__(self, branch_ctx, label):
        self._branch_ctx = branch_ctx
        self._label = label

    def __enter__(self):
        self._branch_ctx._enter_branch(self._label)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._branch_ctx._exit_branch(self._label)
        return False


class BranchCtx:
    def __init__(self, cond):
        self.cond = cond
        self._true_values = {}
        self._false_values = {}
        self._active_branch = None
        self._pipeline = None
        self._if_node = None
        self._registered_nodes = set()

    def __enter__(self):
        if _ACTIVE_PIPELINE is None:
            raise RuntimeError("Branch contexts require an active Pipeline")
        self._pipeline = _ACTIVE_PIPELINE
        if not isinstance(self.cond, (Symbol, NodeWrapper)):
            raise RuntimeError("If condition must be a Symbol or NodeWrapper")
        if self._if_node is None:
            if_node_ptr = _core.create_node("IfNode")
            self._if_node = self._pipeline._register_internal_node(if_node_ptr)
            if isinstance(self.cond, Symbol):
                self._if_node.raw.set_input(self.cond.producer_node)
                self._pipeline._record_connection(
                    self._if_node.raw,
                    _core.ID_FORWARD,
                    0,
                    self.cond.producer_node,
                    self.cond.source_method_id,
                )
            elif isinstance(self.cond, NodeWrapper):
                self._if_node.raw.set_input(self.cond.raw)
                self._pipeline._record_connection(
                    self._if_node.raw,
                    _core.ID_FORWARD,
                    0,
                    self.cond.raw,
                    _core.ID_FORWARD,
                )
        _ACTIVE_BRANCH_CONTEXTS.append(self)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if not _ACTIVE_BRANCH_CONTEXTS or _ACTIVE_BRANCH_CONTEXTS[-1] is not self:
            raise RuntimeError("Branch context stack mismatch")
        _ACTIVE_BRANCH_CONTEXTS.pop()
        return False

    @property
    def true(self):
        return _BranchScope(self, "true")

    @property
    def false(self):
        return _BranchScope(self, "false")

    def assign(self, name, value):
        if self._active_branch == "true":
            self._true_values[name] = value
        elif self._active_branch == "false":
            self._false_values[name] = value
        else:
            raise RuntimeError("Branch assignment outside of active branch context")

    def value(self, name):
        if name not in self._true_values or name not in self._false_values:
            raise RuntimeError(
                f"Branch value '{name}' must be assigned in both true and false branches"
            )
        
        mapping = {
            0: self._true_values[name],
            1: self._false_values[name]
        }
        return MuxSymbol(self._if_node, mapping)

    def _enter_branch(self, label):
        if self._active_branch is not None:
            raise RuntimeError("Nested branch contexts are not supported")
        self._active_branch = label

    def _exit_branch(self, label):
        if self._active_branch != label:
            raise RuntimeError("Branch context mismatch")
        self._active_branch = None

    def _register_node(self, node_wrapper):
        if self._if_node is None or node_wrapper is None:
            return
        if node_wrapper in self._registered_nodes:
            return
        
        try:
            if_idx = self._pipeline._nodes.index(self._if_node)
            node_idx = self._pipeline._nodes.index(node_wrapper)
            if node_idx <= if_idx:
                return
        except ValueError:
            pass

        self._registered_nodes.add(node_wrapper)
        if self._active_branch == "true":
            self._if_node.raw.add_true_branch(node_wrapper.raw)
        elif self._active_branch == "false":
            self._if_node.raw.add_false_branch(node_wrapper.raw)


def if_(cond):
    return BranchCtx(cond)


from . import ast_transform as _ast_transform

_ast_transform.set_default_if_symbol(if_)
ast_transform = _ast_transform.ast_transform
transform_function = _ast_transform.transform_function

# ========== Node Wrapper ==========
class NodeWrapper:
    def __init__(self, raw_node, pipeline=None, registry_name=None, init_args=None, init_kwargs=None):
        self.raw = raw_node
        self.pipeline = pipeline
        self.built = False
        self._registry_name = registry_name
        self._init_args = list(init_args) if init_args is not None else []
        self._init_kwargs = dict(init_kwargs) if init_kwargs is not None else {}
        self._method_config = {
            "order": None,
            "queue_size": {},
        }
        
        self._method_name_to_id = {}
        self._id_to_method_name = {}
        for name in self.raw.exposed_methods:
             h = hash_string(name)
             self._method_name_to_id[name] = h
             self._id_to_method_name[h] = name
        
        if _core.ID_FORWARD not in self._id_to_method_name:
             self._id_to_method_name[_core.ID_FORWARD] = "forward"

    @property
    def registry_name(self):
        return self._registry_name

    @property
    def type_name(self):
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
        return self.__call__()

    def __getattr__(self, name):
        # Check if it is a method
        if name in self._method_name_to_id:
            method_id = self._method_name_to_id[name]
            return self._create_method_proxy(name, method_id)
        
        return getattr(self.raw, name)

    def _create_method_proxy(self, name, method_id):
        def proxy(*args, **kwargs):
            return self._connect(name, method_id, *args, **kwargs)
        return proxy

    def __call__(self, *args, **kwargs):
        return self._connect("forward", _core.ID_FORWARD, *args, **kwargs)

    def set_method_order(self, methods):
        self._method_config["order"] = list(methods)
        return self.raw.set_method_order(methods)

    def set_method_queue_size(self, method, max_queue):
        self._method_config["queue_size"][method] = int(max_queue)
        return self.raw.set_method_queue_size(method, max_queue)

    def _method_name_for_id(self, method_id):
        return self._id_to_method_name.get(method_id)

    def _connect(self, method_name, method_id, *args, **kwargs):
        if _ACTIVE_PIPELINE is None:
             return self.raw.invoke(method_name, *args, **kwargs)

        if self.raw.is_python_node:
            args = self._normalize_python_args(method_name, args, kwargs)
            kwargs = {}
        elif kwargs:
            raise TypeError("Kwargs are only supported for Python nodes inside Pipeline construction")

        if self not in _ACTIVE_PIPELINE._nodes:
            _ACTIVE_PIPELINE._nodes.append(self)
        
        for idx, arg in enumerate(args):
            upstream_node = None
            upstream_method_id = _core.ID_FORWARD
            
            if isinstance(arg, MuxSymbol):
                mux_key = (self.raw, method_id, idx)
                if _ACTIVE_PIPELINE and mux_key in _ACTIVE_PIPELINE._connection_metadata:
                    raise TypeError("Mux input cannot mix with direct connections")
                if _ACTIVE_PIPELINE:
                    _ACTIVE_PIPELINE._mux_inputs.add(mux_key)
                control = arg.control_node.raw
                control_type_info = control.type_info
                if _core.ID_FORWARD not in control_type_info.methods:
                    raise TypeError("Mux control must have a forward output")
                control_output = control_type_info.methods[_core.ID_FORWARD].output_type
                control_name = control_output.name.lower()
                if "pybind11::object" not in control_output.name and control_name not in {"bool", "int", "long", "long int", "long long", "long long int", "int64_t"}:
                    raise TypeError("Mux control packet must be bool or int")
                raw_map = {}
                for k, v in arg.mapping.items():
                    if isinstance(v, Symbol):
                        raw_map[k] = v.producer_node
                        self.raw.set_weak_input(v.producer_node, idx)
                        
                        if _ACTIVE_PIPELINE:
                             _ACTIVE_PIPELINE._record_connection(
                                 self.raw, method_id, idx,
                                 v.producer_node, v.source_method_id
                             )
                    elif isinstance(v, NodeWrapper):
                        if _ACTIVE_PIPELINE and v not in _ACTIVE_PIPELINE._nodes:
                            _ACTIVE_PIPELINE._nodes.append(v)
                        raw_map[k] = v.raw
                        self.raw.set_weak_input(v.raw, idx)
                        
                        if _ACTIVE_PIPELINE:
                             _ACTIVE_PIPELINE._record_connection(
                                 self.raw, method_id, idx,
                                 v.raw, _core.ID_FORWARD
                             )
                
                self.raw.set_input_mux(method_name, idx, control, raw_map)
                if _ACTIVE_PIPELINE:
                    _ACTIVE_PIPELINE._record_mux(self.raw, method_name, method_id, idx, control, raw_map)
                continue

            if isinstance(arg, Symbol):
                upstream_node = arg.producer_node
                upstream_method_id = arg.source_method_id
                
                if arg.tuple_index is not None:
                     up_type = upstream_node.type_info.methods[upstream_method_id].output_type
                     
                     tuple_node_ptr = _core.create_tuple_get_node(up_type, arg.tuple_index)
                     if _ACTIVE_PIPELINE:
                         tuple_wrapper = _ACTIVE_PIPELINE._register_internal_node(tuple_node_ptr)
                         tuple_wrapper.raw.set_input_for("forward", upstream_node)
                         
                         _ACTIVE_PIPELINE._record_connection(
                             tuple_wrapper.raw, _core.ID_FORWARD, 0,
                             upstream_node, upstream_method_id
                         )
                         
                         upstream_node = tuple_wrapper.raw
                         upstream_method_id = _core.ID_FORWARD
                     else:
                         raise RuntimeError("Tuple unpacking requires active pipeline context")

            elif isinstance(arg, NodeWrapper):
                if _ACTIVE_PIPELINE and arg not in _ACTIVE_PIPELINE._nodes:
                    _ACTIVE_PIPELINE._nodes.append(arg)
                
                upstream_node = arg.raw
                upstream_method_id = _core.ID_FORWARD
            
            if upstream_node:
                if _ACTIVE_PIPELINE:
                    mux_key = (self.raw, method_id, idx)
                    if mux_key in _ACTIVE_PIPELINE._mux_inputs:
                        raise TypeError("Direct input cannot mix with mux connections")
                self.raw.set_input_for(method_name, upstream_node, idx)
                if _ACTIVE_PIPELINE:
                    _ACTIVE_PIPELINE._record_connection(
                        self.raw, method_id, idx,
                        upstream_node, upstream_method_id
                    )

        if _ACTIVE_BRANCH_CONTEXTS:
            for ctx in _ACTIVE_BRANCH_CONTEXTS:
                ctx._register_node(self)

        return Symbol(self.raw, source_method_id=method_id)

    def _normalize_python_args(self, method_name, args, kwargs):
        if self.raw.method_has_varargs(method_name):
            if kwargs:
                raise TypeError("Kwargs are not supported for methods with *args/**kwargs")
            return args

        arg_names = self.raw.get_method_arg_names(method_name)
        if not arg_names:
            if kwargs:
                raise TypeError(f"Kwargs are not supported for method '{method_name}'")
            return args

        if len(args) > len(arg_names):
            raise TypeError(f"Too many positional arguments for method '{method_name}'")

        defaults = self.raw.get_method_defaults(method_name)
        name_to_index = {name: idx for idx, name in enumerate(arg_names)}
        filled = [None] * len(arg_names)

        for idx, value in enumerate(args):
            filled[idx] = value

        for key, value in kwargs.items():
            if key not in name_to_index:
                raise TypeError(f"Unknown argument '{key}' for method '{method_name}'")
            idx = name_to_index[key]
            if filled[idx] is not None:
                raise TypeError(f"Argument '{key}' provided multiple times for method '{method_name}'")
            filled[idx] = value

        missing = []
        for i, value in enumerate(filled):
            if value is not None:
                continue
            name = arg_names[i]
            if name in defaults:
                filled[i] = _make_constant_node(defaults[name])
            else:
                missing.append(name)
        if missing:
            raise TypeError(
                f"Missing arguments for method '{method_name}': {', '.join(missing)}"
            )

        return tuple(filled)


# ========== Pipeline ==========
class Pipeline:
    def __init__(self):
        self._graph = _core.ExecutionGraph()
        self._executor = _core.Executor()
        self._nodes = []
        self._internal_nodes = []
        self._validated = False
        self._has_run = False
        self._connection_metadata = {}
        self._mux_inputs = set()
        self._mux_metadata = []
        self._graph.set_error_policy(_core.ErrorPolicy.FailFast)

    def __setattr__(self, name, value):
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

    def set_error_policy(self, policy):
        if not isinstance(policy, _core.ErrorPolicy):
            raise TypeError("policy must be an easywork_core.ErrorPolicy enum value")
        self._graph.set_error_policy(policy)

    def get_error_policy(self):
        return self._graph.get_error_policy()

    def _record_connection(self, consumer, consumer_method, input_idx, producer, producer_method):
        if consumer.type_name == "IfNode" and consumer_method == _core.ID_FORWARD and input_idx == 0:
            if producer_method not in producer.type_info.methods:
                raise TypeError("IfNode condition must be bool or int")
            producer_type = producer.type_info.methods[producer_method].output_type
            name = producer_type.name.lower()
            if "pybind11::object" not in producer_type.name and name not in {"bool", "int", "long", "long int", "long long", "long long int", "int64_t"}:
                raise TypeError("IfNode condition must be bool or int")
        key = (consumer, consumer_method, input_idx)
        val = (producer, producer_method)
        if key not in self._connection_metadata:
            self._connection_metadata[key] = []
        if val in self._connection_metadata[key]:
            return
        self._connection_metadata[key].append(val)

    def _record_mux(self, consumer, method_name, method_id, arg_idx, control, mapping):
        self._mux_metadata.append({
            "consumer": consumer,
            "method_id": method_id,
            "method": method_name,
            "arg_idx": arg_idx,
            "control": control,
            "map": mapping,
        })

    def construct(self):
        pass

    def _clear_topology(self):
        self._reset_connections()
        self._nodes.clear()
        self._clear_internal_nodes()
        self._connection_metadata.clear()
        self._mux_inputs.clear()
        self._mux_metadata.clear()

    def _build_topology(self):
        is_default_construct = self.construct.__func__ is Pipeline.construct
        if self._nodes and is_default_construct:
            _LOG.info("Detected external graph construction, preserving nodes")
            return

        self._clear_topology()
        construct_fn = self.construct
        if not getattr(construct_fn, "__ew_ast_transformed__", False):
            construct_fn = transform_function(construct_fn, strict=True)
        construct_fn = types.MethodType(construct_fn, self)
        self._with_active_pipeline(construct_fn)

    def validate(self):
        _LOG.info("Validating graph types")
        self._build_topology()
        self._validate_types()
        self._validated = True
        return True

    def _assert_exportable(self):
        for node in self._nodes:
            if getattr(node.raw, "is_python_node", False):
                raise RuntimeError("Graph export does not support Python nodes")
            if not getattr(node, "registry_name", None):
                raise RuntimeError("Graph export only supports registered C++ nodes")

    def _json_safe_value(self, value):
        if isinstance(value, (bool, int, float, str)):
            return value
        raise TypeError("Graph export only supports primitive constructor arguments")

    def to_graph_spec(self):
        self._assert_exportable()
        node_id_map = {}
        nodes = []
        for idx, node in enumerate(self._nodes, start=1):
            node_id = f"n{idx}"
            node_id_map[node.raw] = node_id
            args = [self._json_safe_value(v) for v in getattr(node, "_init_args", [])]
            kwargs = {
                str(k): self._json_safe_value(v)
                for k, v in getattr(node, "_init_kwargs", {}).items()
            }
            nodes.append({
                "id": node_id,
                "type": node.registry_name,
                "args": args,
                "kwargs": kwargs,
            })

        mux_edges = set()
        mux = []
        for entry in getattr(self, "_mux_metadata", []):
            consumer_id = node_id_map.get(entry["consumer"])
            control_id = node_id_map.get(entry["control"])
            if consumer_id is None or control_id is None:
                raise RuntimeError("Graph export failed to resolve mux nodes")
            method_name = entry["method"]
            if not method_name:
                raise RuntimeError("Graph export failed to resolve mux method name")
            arg_idx = entry["arg_idx"]
            mapping = {}
            for choice, producer in entry["map"].items():
                producer_id = node_id_map.get(producer)
                if producer_id is None:
                    raise RuntimeError("Graph export failed to resolve mux producer")
                mapping[str(choice)] = producer_id
                mux_edges.add((entry["consumer"], entry["method_id"], arg_idx, producer))
            mux.append({
                "consumer_id": consumer_id,
                "method": method_name,
                "arg_idx": arg_idx,
                "control_id": control_id,
                "map": mapping,
            })

        edges = []
        raw_to_wrapper = {node.raw: node for node in self._nodes}
        for (consumer, method_id, input_idx), producers in self._connection_metadata.items():
            for producer, producer_method in producers:
                if (consumer, method_id, input_idx, producer) in mux_edges:
                    continue
                consumer_id = node_id_map.get(consumer)
                producer_id = node_id_map.get(producer)
                if consumer_id is None or producer_id is None:
                    raise RuntimeError("Graph export failed to resolve edge nodes")
                consumer_wrapper = raw_to_wrapper.get(consumer)
                producer_wrapper = raw_to_wrapper.get(producer)
                consumer_method = consumer_wrapper._method_name_for_id(method_id) if consumer_wrapper else None
                producer_method_name = producer_wrapper._method_name_for_id(producer_method) if producer_wrapper else None
                if not consumer_method or not producer_method_name:
                    raise RuntimeError("Graph export failed to resolve method names")
                edges.append({
                    "from": {"node_id": producer_id, "method": producer_method_name},
                    "to": {"node_id": consumer_id, "method": consumer_method, "arg_idx": input_idx},
                })

        method_config = []
        for node in self._nodes:
            node_id = node_id_map[node.raw]
            config = getattr(node, "_method_config", {})
            order = config.get("order") if config else None
            if order:
                method_config.append({
                    "node_id": node_id,
                    "order": list(order),
                })
            queue_size = config.get("queue_size", {}) if config else {}
            for method, size in queue_size.items():
                method_config.append({
                    "node_id": node_id,
                    "method": method,
                    "queue_size": int(size),
                })

        return {
            "schema_version": 1,
            "metadata": {
                "easywork_version": "unknown",
                "export_time": datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
            },
            "nodes": nodes,
            "edges": edges,
            "mux": mux,
            "method_config": method_config,
        }

    def export(self, path):
        spec = self.to_graph_spec()
        with open(path, "w", encoding="utf-8") as f:
            json.dump(spec, f, ensure_ascii=True, indent=2)
        return path

    def _validate_types(self):
        if hasattr(_core, "register_arithmetic_conversions"):
            _core.register_arithmetic_conversions()
        raw_to_wrapper = {node.raw: node for node in self._nodes}

        def _is_convertible(from_type, to_type):
            if from_type == to_type:
                return True
            if "pybind11::object" in to_type.name:
                return True
            if hasattr(_core, "can_convert"):
                return _core.can_convert(from_type, to_type)
            return False

        def _is_if_condition_type(type_info):
            name = type_info.name.lower()
            if "pybind11::object" in type_info.name:
                return True
            return name in {"bool", "int", "long", "long int", "long long", "long long int", "int64_t"}

        for (consumer, method_id, input_idx), producers in self._connection_metadata.items():
            type_info = consumer.type_info
            if method_id not in type_info.methods:
                method_name = str(method_id)
                wrapper = raw_to_wrapper.get(consumer)
                if wrapper:
                    method_name = wrapper._id_to_method_name.get(method_id, method_name)
                raise TypeError(f"Method not found: {method_name}")

            method_info = type_info.methods[method_id]
            if input_idx >= len(method_info.input_types):
                method_name = str(method_id)
                wrapper = raw_to_wrapper.get(consumer)
                if wrapper:
                    method_name = wrapper._id_to_method_name.get(method_id, method_name)
                raise TypeError(
                    f"Type mismatch on method '{method_name}': argument index out of range"
                )

            expected_type = method_info.input_types[input_idx]
            producer_types = []
            for producer, producer_method in producers:
                producer_type_info = producer.type_info
                if producer_method not in producer_type_info.methods:
                    producer_type = _core.TypeInfo()
                else:
                    producer_type = producer_type_info.methods[producer_method].output_type
                producer_types.append(producer_type)

            for producer_type in producer_types:
                if consumer.type_name == "IfNode" and method_id == _core.ID_FORWARD and input_idx == 0:
                    if not _is_if_condition_type(producer_type):
                        raise TypeError("IfNode condition must be bool or int")
                    if producer_type.name.lower() in {"int", "long", "long int", "long long", "long long int", "int64_t"}:
                        continue
                if not _is_convertible(producer_type, expected_type):
                    method_name = str(method_id)
                    wrapper = raw_to_wrapper.get(consumer)
                    if wrapper:
                        method_name = wrapper._id_to_method_name.get(method_id, method_name)
                    raise TypeError(
                        "Type mismatch on method '" + method_name +
                        "': expected " + expected_type.name +
                        ", got " + producer_type.name
                    )

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
        if not self._validated:
            self._build_topology()

        if self._has_run:
            _LOG.info("Resetting Graph for re-run")
            self._graph.reset()
            for node in self._nodes:
                node.built = False

        _LOG.info("Materializing Graph (%d nodes)", len(self._nodes))
        for node in self._nodes:
            if not node.built:
                node.raw.build(self._graph)
                node.built = True

        _LOG.info("Connecting Edges")
        for node in self._nodes:
            node.raw.connect()

        for node in self._nodes:
            node.raw.activate()

        self._ensure_all_open()
        _LOG.info("Starting Executor")
        try:
            self._executor.run(self._graph)
        except KeyboardInterrupt:
            _LOG.warning("Stopping on KeyboardInterrupt")
        self._has_run = True
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
        registry_name = None
        try:
            type_name = cpp_node.type_name
            if _core._NodeRegistry.instance().is_registered(type_name):
                registry_name = type_name
        except Exception:
            registry_name = None
        wrapper = NodeWrapper(cpp_node, registry_name=registry_name)
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

class ModuleProxy:
    def __getattr__(self, name):
        if _core._NodeRegistry.instance().is_registered(name):
            def factory(*args, **kwargs):
                if name == "NumberSource":
                    step_value = None
                    if len(args) >= 3:
                        step_value = args[2]
                    elif "step" in kwargs:
                        step_value = kwargs["step"]
                    if step_value is not None:
                        if not isinstance(step_value, int):
                            raise RuntimeError("Failed to parse argument 'step'")
                        if step_value == 0:
                            raise RuntimeError("NumberSource step cannot be 0")
                node = _core.create_node(name, *args, **kwargs)
                return NodeWrapper(node, registry_name=name, init_args=args, init_kwargs=kwargs)
            return factory
        raise AttributeError(f"Node type '{name}' not found")

    def __dir__(self):
        return _core._NodeRegistry.instance().registered_nodes()

module = ModuleProxy()


# ========== Python Node Registration ==========
class PythonNode:
    __ew_methods__ = ("forward",)

    def __init__(self, *args, **kwargs):
        pass
    
    @property
    def raw(self):
        if "_ew_cpp_node" not in self.__dict__:
            self._ew_cpp_node = _core.create_node_from_instance(self)
        return self._ew_cpp_node

    @property
    def _wrapper(self):
        if "_ew_wrapper" not in self.__dict__:
            self._ew_wrapper = NodeWrapper(self.raw)
        return self._ew_wrapper
        
    def __call__(self, *args, **kwargs):
        if _ACTIVE_PIPELINE is None:
            return self.forward(*args, **kwargs)
        return self._wrapper(*args, **kwargs)
        
    def __getattr__(self, name):
         if name.startswith("_"):
             raise AttributeError(name)

         if _ACTIVE_PIPELINE:
             return getattr(self._wrapper, name)
         
         raise AttributeError(f"'{type(self).__name__}' object has no attribute '{name}'")

    def forward(self, *args, **kwargs):
        raise NotImplementedError("PythonNode must implement forward()")

# Remove old registration decorator
# def register_node(name=None): ...

class _PyConst(PythonNode):
    __ew_methods__ = ("forward",)
    
    def __init__(self, value):
        self._value = value

    def forward(self):
        return self._value


def _make_constant_node(value):
    return _PyConst(value)
