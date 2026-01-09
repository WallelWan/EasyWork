import sys
import os
import numpy as np
import cv2

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../python")))
import easywork as ew

class FaceDetectApp(ew.Pipeline):
    def __init__(self):
        super().__init__()
        # 使用新的统一 API (工厂模式)
        self.cam = ew.module.CameraSource(device_id=-1)
        self.processor = ew.PyFunc(self.process_frame)
        self.writer = ew.module.VideoWriterSink("class_output.avi")
        self.sink = ew.module.NullSink() # Just to show branching

    def process_frame(self, frame):
        # User logic
        img = np.array(frame, copy=False)
        mean_color = np.mean(img, axis=(0,1))
        # Draw a box
        cv2.rectangle(img, (100, 100), (300, 300), (255, 255, 0), 2)
        print(f"[App] Processing frame... Mean: {mean_color}", flush=True)
        return frame

    def construct(self):
        # 定义拓扑 (The "Forward" pass)
        x = self.cam.read()

        # 像 PyTorch 一样调用处理器节点!
        y = self.processor(x)

        # Fork logic: 写入文件并发送到 null sink
        self.writer.write(y)
        self.sink.consume(y)

if __name__ == "__main__":
    print("Initializing App (C++20 Factory Pattern)...")
    app = FaceDetectApp()

    print("Running App...")
    app.run()

    print("Finished.")
