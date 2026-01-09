#pragma once

#include "core_tbb.h"
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <pybind11/pybind11.h>

namespace easywork {

// 1. Camera Source (Supports Mock)
class CameraSource : public SourceNode {
public:
    CameraSource(int id) : device_id_(id) {
        if (id >= 0) {
            cap_.open(id);
            if (!cap_.isOpened()) {
                spdlog::error("Failed to open camera {}, will use MOCK mode", id);
                use_mock_ = true;
            } else {
                spdlog::info("Camera {} opened", id);
            }
        } else {
            use_mock_ = true;
        }
    }

    Frame generate() override {
        // Limit run count for testing
        if (limit_ > 0 && count_ >= limit_) {
            spdlog::info("Limit reached ({}), stopping source.", limit_);
            return nullptr;
        }
        
        auto frame = make_frame(640, 480, CV_8UC3);
        
        if (use_mock_) {
            // Generate Red/Blue/White pattern
            int step = count_ % 3;
            if (step == 0) frame->mat.setTo(cv::Scalar(0, 0, 255)); // Red
            else if (step == 1) frame->mat.setTo(cv::Scalar(255, 0, 0)); // Blue
            else frame->mat.setTo(cv::Scalar(255, 255, 255)); // White
            
            spdlog::trace("Generated Mock Frame #{}", count_);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        } else {
            cap_ >> frame->mat; 
            if (frame->mat.empty()) {
                spdlog::warn("Empty frame from camera");
                return nullptr;
            }
        }
        
        frame->timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        count_++;
        return frame;
    }
    
    void set_limit(int n) { limit_ = n; }

private:
    cv::VideoCapture cap_;
    int device_id_;
    bool use_mock_ = false;
    int count_ = 0;
    int limit_ = -1; // -1 = infinite
};

// 2. Image Processor
class CannyFilter : public ProcessNode {
public:
    Frame process(Frame input) override {
        auto output = make_frame(input->width, input->height, CV_8UC1);
        cv::cvtColor(input->mat, output->mat, cv::COLOR_BGR2GRAY);
        cv::Canny(output->mat, output->mat, 100, 200);
        return output;
    }
};

// 3. Null Sink (For testing stability)
class NullSink : public SinkNode {
public:
    NullSink() {}
    void consume(Frame input) override {
        // Do nothing, just keep the pipeline flowing
        if (input) {
            spdlog::trace("NullSink consumed frame");
        }
    }
};

// 3. Video Writer Sink
class VideoWriterSink : public SinkNode {
public:
    VideoWriterSink(std::string filename) : filename_(filename) {}

    void consume(Frame input) override {
        if (!input || input->mat.empty()) return;

        if (!writer_.isOpened()) {
            writer_.open(filename_, cv::VideoWriter::fourcc('M','J','P','G'), 
                         30.0, input->mat.size(), input->mat.channels() == 3);
            if (!writer_.isOpened()) {
                spdlog::error("Failed to open video writer: {}", filename_);
                return;
            }
            spdlog::info("Recording to {}", filename_);
        }

        writer_.write(input->mat);
    }

private:
    std::string filename_;
    cv::VideoWriter writer_;
};

// 4. Python Custom Function Node
class PyFuncNode : public ProcessNode {
public:
    explicit PyFuncNode(pybind11::function func) : func_(func) {}

    Frame process(Frame input) override {
        spdlog::trace("PyFuncNode: processing frame...");
        pybind11::gil_scoped_acquire acquire;
        try {
            pybind11::object py_frame = pybind11::cast(input);
            pybind11::object result = func_(py_frame);
            return result.cast<Frame>();
        } catch (const std::exception& e) {
            spdlog::error("Python execution failed: {}", e.what());
            return input;
        }
    }

private:
    pybind11::function func_;
};

} // namespace easywork
