import sys
import os
import numpy as np
import cv2

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../python")))
import easywork as ew

def my_python_processor(frame):
    # This runs inside C++ thread!
    try:
        # Zero-copy view
        img = np.array(frame, copy=False)
        
        # Calculate mean color
        mean_color = np.mean(img, axis=(0,1))
        
        # We use standard print, which might be buffered, so we flush
        print(f"[Python Op] Frame {frame.width}x{frame.height} Mean: {mean_color}", flush=True)
        
        # Draw something
        cv2.circle(img, (320, 240), 50, (0, 255, 0), -1)
    except Exception as e:
        print(f"Error in python op: {e}")
        
    return frame

def hybrid_pipeline(cam, my_op, sink):
    raw = cam.read()
    processed = my_op(raw) 
    sink.consume(processed)

if __name__ == "__main__":
    print("Initializing Hybrid System...")
    sys_instance = ew.System()
    
    # 1. Mock Camera (Run 10 frames)
    cam = sys_instance.add(ew.Camera(device_id=-1, limit=10))
    
    # 2. Python Op
    py_node = sys_instance.add(ew.PyFunc(my_python_processor))
    
    # 3. Null Sink
    sink = sys_instance.add(ew.NullSink())
    
    print("Running Pipeline (10 frames)...")
    sys_instance.run(hybrid_pipeline, inputs={
        "cam": cam, 
        "my_op": py_node.call,
        "sink": sink
    })
    
    print("Done!")