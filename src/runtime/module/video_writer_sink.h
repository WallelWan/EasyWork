#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"
#include <opencv2/videoio.hpp>
#include <spdlog/spdlog.h>
#include <string>

namespace easywork {

/**
 * @brief Video writer sink that saves frames to a video file.
 *
 * Automatically opens the video file on first frame received.
 * Uses MJPEG codec by default with 30 FPS.
 */
class VideoWriterSink : public FunctionNode {
public:
    /**
     * @brief Construct a new Video Writer Sink object
     * @param filename Output video file path
     */
    explicit VideoWriterSink(std::string filename) : filename_(std::move(filename)) {}

    /**
     * @brief Forward pass: Write a frame to the video file
     * @param input Input frame to write
     * @return nullptr to terminate the flow
     */
    Frame forward(Frame input) override {
        if (!input || input->mat.empty()) return nullptr;

        if (!writer_.isOpened()) {
            writer_.open(filename_, cv::VideoWriter::fourcc('M','J','P','G'),
                         30.0, input->mat.size(), input->mat.channels() == 3);
            if (!writer_.isOpened()) {
                spdlog::error("Failed to open video writer: {}", filename_);
                return nullptr;
            }
            spdlog::info("Recording to {}", filename_);
        }

        writer_.write(input->mat);
        return nullptr;  // Sink nodes return nullptr to terminate flow
    }

private:
    std::string filename_;
    cv::VideoWriter writer_;
};

// ========== Factory Registration ==========
// Register with 1 parameter: std::string filename (default: "output.avi")
EW_REGISTER_NODE_1(VideoWriterSink, "VideoWriterSink", std::string, filename, std::string("output.avi"))

} // namespace easywork
