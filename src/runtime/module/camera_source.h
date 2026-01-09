#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"
#include <opencv2/videoio.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace easywork {

/**
 * @brief Camera source node that captures frames from a camera device.
 *
 * Supports both real camera capture and mock mode for testing.
 * In mock mode, generates Red/Blue/White colored frames in sequence.
 */
class CameraSource : public InputNode {
public:
    /**
     * @brief Construct a new Camera Source object
     * @param id Camera device ID (-1 for mock mode, >=0 for real camera)
     * @param limit Maximum frames to emit (-1 for infinite)
     */
    explicit CameraSource(int id, int limit = -1)
        : device_id_(id), limit_(limit) {
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

    /**
     * @brief Forward pass: Generate a frame from camera or mock mode
     * @param fc Optional flow control for stream termination
     * @return Frame buffer, or nullptr if limit reached or camera disconnected
     */
    Frame forward(tbb::flow_control* fc = nullptr) override {
        // Limit run count for testing
        if (limit_ > 0 && count_ >= limit_) {
            spdlog::info("Limit reached ({}), stopping source.", limit_);
            if (fc) fc->stop();
            return nullptr;
        }

        auto frame = make_frame(640, 480, CV_8UC3);

        if (use_mock_) {
            // Generate Red/Blue/White pattern for testing
            int step = count_ % 3;
            if (step == 0) frame->mat.setTo(cv::Scalar(0, 0, 255));      // Red (BGR)
            else if (step == 1) frame->mat.setTo(cv::Scalar(255, 0, 0)); // Blue
            else frame->mat.setTo(cv::Scalar(255, 255, 255));            // White

            spdlog::trace("Generated Mock Frame #{}", count_);
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
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

    /**
     * @brief Set maximum number of frames to generate (-1 for infinite)
     * @param n Maximum frame count
     */
    void set_limit(int n) { limit_ = n; }

private:
    cv::VideoCapture cap_;
    int device_id_;
    bool use_mock_ = false;
    int count_ = 0;
    int limit_ = -1; // -1 = infinite
};

// ========== Factory Registration ==========
// Register with 2 parameters: device_id, limit
EW_REGISTER_NODE_2(CameraSource, "CameraSource",
                   int, device_id, -1,
                   int, limit, -1)

} // namespace easywork
