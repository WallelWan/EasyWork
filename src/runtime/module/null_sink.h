#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"
#include <spdlog/spdlog.h>

namespace easywork {

/**
 * @brief Null sink node that consumes frames without processing.
 *
 * Useful for testing pipeline stability and as a debug sink.
 * Simply traces frame consumption without any side effects.
 */
class NullSink : public FunctionNode {
public:
    NullSink() = default;

    /**
     * @brief Forward pass: Consume a frame (does nothing)
     * @param input Input frame (may be nullptr)
     * @return nullptr to terminate the flow
     */
    Frame forward(Frame input) override {
        if (input) {
            spdlog::trace("NullSink consumed frame");
        }
        return nullptr;  // Sink nodes return nullptr to terminate flow
    }
};

// ========== Factory Registration ==========
// Register with 0 parameters (default constructor)
EW_REGISTER_NODE(NullSink, "NullSink")

} // namespace easywork
