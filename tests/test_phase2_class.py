import sys
import os
import numpy as np
import cv2

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../python")))
import easywork as ew

class FaceDetectApp(ew.Pipeline):
    def __init__(self):
        super().__init__()
        # 1. Define Nodes (Implicitly registered)
        self.cam = ew.Camera(device_id=-1, limit=15)
        self.processor = ew.PyFunc(self.process_frame)
        self.writer = ew.VideoWriter("class_output.avi")
        self.sink = ew.NullSink() # Just to show branching

    def process_frame(self, frame):
        # User logic
        img = np.array(frame, copy=False)
        mean_color = np.mean(img, axis=(0,1))
        # Draw a box
        cv2.rectangle(img, (100, 100), (300, 300), (255, 255, 0), 2)
        print(f"[App] Processing frame... Mean: {mean_color}", flush=True)
        return frame

    def construct(self):
        # 2. Define Topology (The "Forward" pass)
        x = self.cam.read()
        
        # Call the processor node like a function!
        y = self.processor(x)
        
        # Fork logic: Write to file AND send to null sink
        self.writer.write(y)
        self.sink.consume(y)

if __name__ == "__main__":
    print("Initializing App (PyTorch Style)...")
    app = FaceDetectApp()
    
    print("Running App...")
    app.run()
    
    print("Finished.")
