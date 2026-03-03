#pragma once

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "runtime/core/node.h"

namespace easywork {

class Executor {
public:
    void open(const std::vector<Node*>& nodes) {
        for (auto* node : nodes) {
            if (node) {
                node->Open();
            }
        }
    }

    void close(const std::vector<Node*>& nodes) {
        for (auto* node : nodes) {
            if (node) {
                node->Close();
            }
        }
    }

    void run(ExecutionGraph& g) {
        g.keep_running = true;
        g.Lock();
        const auto start = std::chrono::steady_clock::now();
        size_t iterations = 0;
        LogRuntime(RuntimeLogLevel::Info, "Executor started", {
            {"event", "executor_start"},
        });
        while (g.keep_running) {
            g.skip_current = false;
            ++iterations;
            g.executor.run(g.taskflow).wait();
            if (g.skip_current && g.error_policy == ErrorPolicy::SkipCurrentData) {
                g.ClearOutputsAndBuffers();
                LogRuntime(RuntimeLogLevel::Warn, "Skipped current data after error", {
                    {"event", "skip_current_data"},
                    {"error_code", "EW_SKIP_CURRENT_DATA"},
                });
            }
        }
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
        std::string stop_reason = "node_stop_or_completion";
        if (!g.LastError().empty()) {
            stop_reason = g.GetErrorPolicy() == ErrorPolicy::FailFast ? "failfast_error"
                                                                       : "error_skip_current";
        }
        LogRuntime(RuntimeLogLevel::Info, "Executor stopped", {
            {"event", "executor_summary"},
            {"iterations", std::to_string(iterations)},
            {"elapsed_ms", std::to_string(elapsed_ms)},
            {"error_count", std::to_string(g.ErrorCount())},
            {"last_error_code", g.LastErrorCodeName()},
            {"stop_reason", stop_reason},
        });
        g.Unlock();
    }
};

inline void ExecutionGraph::RegisterNode(Node* node) {
    if (!node) {
        return;
    }
    if (std::find(nodes_.begin(), nodes_.end(), node) == nodes_.end()) {
        nodes_.push_back(node);
    }
}

inline void ExecutionGraph::ClearOutputsAndBuffers() {
    for (auto* node : nodes_) {
        if (!node) {
            continue;
        }
        node->ClearOutput();
        node->ClearBuffers();
    }
}

} // namespace easywork
