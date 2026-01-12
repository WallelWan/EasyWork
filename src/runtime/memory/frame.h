#pragma once

#include <memory>
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include <spdlog/spdlog.h>

namespace easywork {

enum class DeviceType {
    CPU,
    CUDA,
    Vulkan
};

// The payload carrying the actual data.
// In Phase 2, we wrap cv::Mat for convenience, but design it to be replaced easily.
/**
 * @brief Represents an image/data frame buffer.
 * 
 * Currently backed by OpenCV cv::Mat for easy memory management and processing.
 * Supports passing data pointers to Python (NumPy) via buffer protocol.
 */
struct FrameBuffer {
    FrameBuffer(int w, int h, int type) {
        // Allocate CPU memory using OpenCV (aligned malloc)
        mat = cv::Mat(h, w, type);
        data = mat.data;
        width = w;
        height = h;
        stride = mat.step[0];
        device = DeviceType::CPU;
        spdlog::trace("FrameBuffer Allocated: {}x{}", w, h);
    }

    ~FrameBuffer() {
        spdlog::trace("FrameBuffer Released: {}x{}", width, height);
        // cv::Mat destructor handles memory freeing
    }

    // Backend-specific handle
    cv::Mat mat; 
    
    // Generic metadata
    void* data = nullptr;
    int width = 0;
    int height = 0;
    size_t stride = 0;
    DeviceType device = DeviceType::CPU;
    uint64_t timestamp = 0; // nanoseconds
};

// The handle passed between nodes.
// Use shared_ptr to allow multiple consumers (fork graph).
using Frame = std::shared_ptr<FrameBuffer>;

/// Helper to create a new FrameBuffer.
inline Frame make_frame(int w, int h, int type = CV_8UC3) {
    return std::make_shared<FrameBuffer>(w, h, type);
}

} // namespace easywork
