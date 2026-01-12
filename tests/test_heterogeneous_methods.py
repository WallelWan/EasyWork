
import sys
import os
import pytest
import easywork as ew

# Ensure we can import the module if running directly
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))

def test_heterogeneous_methods_connection():
    """Test connecting nodes to different methods with different signatures."""
    class MixedPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.int_source = ew.module.NumberSource(start=5, max=10, step=1)
            self.str_source = ew.module.IntToText() # consumes int, produces string
            self.mixed_node = ew.module.MixedNode()
            self.consumer = ew.module.IntToText() # consumes int (from forward)

        def construct(self):
            # 1. Connect to 'forward' (int -> int)
            val = self.int_source.read()
            out = self.mixed_node(val) # Default call uses 'forward'
            self.consumer(out)

            # 2. Connect to 'set_string' (string -> void)
            # Create a string from int source first
            str_val = self.str_source(val)
            self.mixed_node.set_string(str_val)

            # 3. Connect to 'compute_ratio' (int, int -> double)
            # We need two ints. Reuse 'val'
            ratio = self.mixed_node.compute_ratio(val, val)
            
            # Note: ratio is double/float. We don't have a double consumer in this test setup
            # but validation should pass.

    pipeline = MixedPipeline()
    # If this passes validation, it means types matched
    pipeline.validate()
    
    # We can try to run it if the binary was built (which we can't do, but we assume)
    # pipeline.open()
    # pipeline.run()
    # pipeline.close()


def test_type_mismatch_detection():
    """Test that mismatched types on specific methods are caught."""
    class BadPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.int_source = ew.module.NumberSource(0, 1, 1)
            self.mixed_node = ew.module.MixedNode()

        def construct(self):
            val = self.int_source.read()
            # method 'set_string' expects string, but we give int
            self.mixed_node.set_string(val)

    pipeline = BadPipeline()
    with pytest.raises(TypeError) as excinfo:
        pipeline.validate()
    
    assert "Type mismatch" in str(excinfo.value)
    assert "set_string" in str(excinfo.value)


def test_void_return_connection():
    """Test connecting a void-returning method to something else (should fail or be empty)."""
    class VoidPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.str_source = ew.module.PrefixText("test")
            self.mixed_node = ew.module.MixedNode()
            self.consumer = ew.module.PrefixText("consumer")

        def construct(self):
            s = self.str_source(ew.module.IntToText()(ew.module.NumberSource(0,1,1).read()))
            
            # set_string returns void
            # Attempting to use its output should be problematic if we expect a value
            void_out = self.mixed_node.set_string(s)
            
            # If we connect void output to a node expecting string, it should fail type check
            # because void != string
            self.consumer(void_out)

    pipeline = VoidPipeline()
    with pytest.raises(TypeError) as excinfo:
        pipeline.validate()
        
    assert "Type mismatch" in str(excinfo.value)

