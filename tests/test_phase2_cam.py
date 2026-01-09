import sys
import os
import time

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../python")))
import easywork as ew

def cam_pipeline(cam, filter, writer):
    # Pure Python definition
    frame = cam.read()
    edges = filter.process(frame)
    writer.write(edges)

if __name__ == "__main__":
    sys_instance = ew.System()
    
    # Use ID 0. If fails, try passing a video file path if you have one.
    # On headless server, /dev/video0 might not exist.
    # If it fails, the log will show "Failed to open camera".
    cam = sys_instance.add(ew.Camera(0)) 
    
    edge_filter = sys_instance.add(ew.Canny())
    
    # Save to 'output.avi'
    writer = sys_instance.add(ew.VideoWriter("output.avi"))
    
    print("Starting Video Pipeline (Recording to output.avi)...")
    try:
        sys_instance.run(cam_pipeline, inputs={"cam": cam, "filter": edge_filter, "writer": writer})
    except KeyboardInterrupt:
        pass
