#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"
#include <opencv2/imgproc.hpp>

namespace easywork {

/**
 * @brief Canny edge detection processor node.
 *
 * Applies Canny edge detection algorithm to input frames.
 * Converts to grayscale first, then applies edge detection.
 */
class CannyFilter : public FunctionNode {
public:
    /**
     * @brief Forward pass: Process a frame with Canny edge detection
     * @param input Input frame
     * @return Processed frame with edges detected (grayscale)
     */
    Frame forward(Frame input) override {
        auto output = make_frame(input->width, input->height, CV_8UC1);
        cv::cvtColor(input->mat, output->mat, cv::COLOR_BGR2GRAY);
        cv::Canny(output->mat, output->mat, 100, 200);
        return output;
    }
};

// ========== Factory Registration ==========
// Register with 0 parameters (default constructor)
EW_REGISTER_NODE(CannyFilter, "CannyFilter")

} // namespace easywork
