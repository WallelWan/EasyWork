from . import easywork_core as _core
import time

# --- Symbol & Wrappers ---
class Symbol:
    def __init__(self, producer_node):
        self.producer_node = producer_node

class NodeWrapper:
    """Base class for all Python node wrappers."""
    def __init__(self, cpp_node):
        self._cpp_node = cpp_node
        self.built = False 
    
    @property
    def raw(self):
        return self._cpp_node
    
    # Enable calling the node directly (like module(x))
    # This requires subclasses to implement specific logic or we map it to a default method
    def __call__(self, *args, **kwargs):
        if hasattr(self, 'process'):
            return self.process(*args, **kwargs)
        if hasattr(self, 'call'):
            return self.call(*args, **kwargs)
        raise NotImplementedError("This node is not callable (no process/call method)")

# --- Concrete Nodes ---

class Camera(NodeWrapper):
    def __init__(self, device_id=-1, limit=-1):
        super().__init__(_core.CameraSource(device_id))
        if limit > 0:
            self._cpp_node.set_limit(limit)

    def read(self):
        return Symbol(self._cpp_node)

class Canny(NodeWrapper):
    def __init__(self):
        super().__init__(_core.CannyFilter())

    def process(self, input_symbol):
        self._cpp_node.set_input(input_symbol.producer_node)
        return Symbol(self._cpp_node)

class NullSink(NodeWrapper):
    def __init__(self):
        super().__init__(_core.NullSink())

    def consume(self, input_symbol):
        self._cpp_node.set_input(input_symbol.producer_node)

class VideoWriter(NodeWrapper):
    def __init__(self, filename="output.avi"):
        super().__init__(_core.VideoWriterSink(filename))

    def write(self, input_symbol):
        self._cpp_node.set_input(input_symbol.producer_node)

class PyFunc(NodeWrapper):
    def __init__(self, py_callable):
        super().__init__(_core.PyFuncNode(py_callable))

    def call(self, input_symbol):
        self._cpp_node.set_input(input_symbol.producer_node)
        return Symbol(self._cpp_node)

# --- The New "PyTorch-like" Pipeline ---

class Pipeline:
    def __init__(self):
        self._graph = _core.ExecutionGraph()
        self._executor = _core.Executor()
        self._nodes = [] # Auto-registered nodes

    def __setattr__(self, name, value):
        """Magic: Automatically register NodeWrapper instances assigned to self."""
        if isinstance(value, NodeWrapper):
            # Check if we already have it (to avoid duplicates if re-assigned)
            if value not in self._nodes: # Note: _nodes might not be init yet if super().__init__ not called
                # We need to be careful about accessing self._nodes before it exists
                pass 
        
        # Standard assignment
        object.__setattr__(self, name, value)
        
        # Registration logic (safe access)
        if isinstance(value, NodeWrapper):
            if not hasattr(self, '_nodes'):
                self._nodes = []
            if value not in self._nodes:
                self._nodes.append(value)

    def construct(self):
        """User should override this to define connections (topology)."""
        raise NotImplementedError("You must implement construct() method")

    def run(self):
        print("[EasyWork] Tracing topology...")
        # 1. Trace: Execute user's connection logic
        self.construct()
        
        # 2. Build: Create C++ TBB nodes
        print(f"[EasyWork] Materializing Graph ({len(self._nodes)} nodes)...")
        for node in self._nodes:
            if not node.built:
                node.raw.build(self._graph)
                node.built = True
        
        # 3. Connect: Link TBB edges
        print("[EasyWork] Connecting Edges...")
        for node in self._nodes:
            node.raw.connect()
            
        # 4. Execute
        print("[EasyWork] Starting Executor...")
        try:
            self._executor.run(self._graph)
        except KeyboardInterrupt:
            print("\n[EasyWork] Stopping...")

# Keep System for backward compatibility (optional)
class System(Pipeline):
    def add(self, node):
        self._nodes.append(node)
        return node
    
    def run(self, func, inputs):
        # Shim to support old functional style
        self.construct = lambda: func(**inputs)
        super().run()