import sys
import os

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../python")))

import easywork as ew

def my_pipeline(src, process, sink):
    # This is the desired syntax!
    # It looks like procedural Python: read -> add -> show
    
    val = src.read()      # Returns a Symbol representing the number
    res = process.add(val)# "Passes" the number to the adder
    sink.show(res)        # "Passes" the result to the screen

if __name__ == "__main__":
    print("Initializing System...")
    sys_instance = ew.System()
    
    # Instantiate our "Smart" Wrappers
    n1 = sys_instance.add(ew.IntSource())
    n2 = sys_instance.add(ew.AddNode())
    n3 = sys_instance.add(ew.PrintSink())
    
    print("Running Pipeline...")
    try:
        sys_instance.run(my_pipeline, inputs={"src": n1, "process": n2, "sink": n3})
    except KeyboardInterrupt:
        pass