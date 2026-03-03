#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <taskflow/taskflow.hpp>

#include "runtime/core/error_codes.h"
#include "runtime/core/ids.h"
#include "runtime/core/logger.h"

namespace easywork {

class Node;

class ExecutionGraph {
public:
    tf::Taskflow taskflow;
    tf::Executor executor;
    std::atomic<bool> keep_running{true};
    std::atomic<bool> skip_current{false};
    std::atomic<bool> locked{false};
    ErrorPolicy error_policy{ErrorPolicy::FailFast};

    void Reset() {
        taskflow.clear();
        keep_running = true;
        skip_current = false;
        locked = false;
        ClearErrors();
        nodes_.clear();
    }

    void SetErrorPolicy(ErrorPolicy policy) {
        error_policy = policy;
    }

    ErrorPolicy GetErrorPolicy() const {
        return error_policy;
    }

    void ReportError(ErrorCode code,
                     const std::string& message,
                     std::unordered_map<std::string, std::string> fields = {}) {
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = message;
            ++error_count_;
            last_error_code_.store(static_cast<std::uint32_t>(code));
        }
        if (!fields.contains("error_code")) {
            fields.emplace("error_code", std::string(ErrorCodeName(code)));
        }
        fields["detail"] = message;
        LogRuntime(RuntimeLogLevel::Error, "Runtime error", std::move(fields));
        if (error_policy == ErrorPolicy::FailFast) {
            keep_running = false;
        } else {
            skip_current = true;
        }
    }

    void ClearErrors() {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_.clear();
        error_count_ = 0;
        last_error_code_.store(static_cast<std::uint32_t>(ErrorCode::Ok));
    }

    std::string LastError() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

    size_t ErrorCount() const {
        return error_count_.load();
    }

    ErrorCode LastErrorCode() const {
        return static_cast<ErrorCode>(last_error_code_.load());
    }

    std::string LastErrorCodeName() const {
        return std::string(ErrorCodeName(LastErrorCode()));
    }

    void RegisterNode(Node* node);
    void ClearOutputsAndBuffers();

    void Lock() { locked = true; }
    void Unlock() { locked = false; }
    bool IsLocked() const { return locked.load(); }

private:
    mutable std::mutex error_mutex_;
    std::string last_error_;
    std::atomic<size_t> error_count_{0};
    std::atomic<std::uint32_t> last_error_code_{static_cast<std::uint32_t>(ErrorCode::Ok)};
    std::vector<Node*> nodes_;
};

struct FlowControl {
    ExecutionGraph* graph;

    void stop() {
        if (graph) {
            graph->keep_running = false;
        }
    }
};

} // namespace easywork
