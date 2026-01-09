#pragma once

#include "../core_tbb.h"
#include <spdlog/spdlog.h>
#include <pybind11/pybind11.h>

namespace easywork {

/**
 * @brief Python function node that wraps a Python callable.
 *
 * Allows custom Python logic to be injected into the C++ pipeline.
 * Acquires GIL during execution to ensure thread safety.
 *
 * Note: This node requires manual binding and is NOT registered in the factory.
 */
class PyFuncNode : public FunctionNode {
public:
    /**
     * @brief Construct a new PyFunc Node object
     * @param func Python callable object
     */
    explicit PyFuncNode(pybind11::function func) : func_(std::move(func)) {}

    /**
     * @brief Forward pass: Process frame using Python function
     * @param input Input frame
     * @return Processed frame (or original if Python function fails)
     */
    Frame forward(Frame input) override {
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
