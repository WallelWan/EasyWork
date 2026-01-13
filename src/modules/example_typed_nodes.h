#pragma once

#include "runtime/core/core.h"
#include "runtime/registry/node_registry.h"
#include "runtime/registry/macros.h"
#include <atomic>
#include <string>
#include <tuple>
#include <unordered_map>

namespace easywork {

class NumberSource : public BaseNode<NumberSource> {
public:
    NumberSource(int start, int max, int step)
        : current_(start), max_(max), step_(step) {}

    int forward() {
        if (current_ > max_) {
            Stop();
            return 0;
        }
        int value = current_;
        if (current_ >= max_) {
            Stop();
        }
        current_ += step_;
        return value;
    }

    EW_ENABLE_METHODS(forward)

private:
    int current_;
    int max_;
    int step_;
};

EW_REGISTER_NODE(NumberSource, "NumberSource",
    Arg("start", 0),
    Arg("max", 10),
    Arg("step", 1)
)

/// Multiplies integer input by a constant factor.
class MultiplyBy : public BaseNode<MultiplyBy> {
public:
    explicit MultiplyBy(int factor) : factor_(factor) {}

    int forward(int input) {
        return input * factor_;
    }

    EW_ENABLE_METHODS(forward)

private:
    int factor_;
};

EW_REGISTER_NODE(MultiplyBy, "MultiplyBy", Arg("factor", 2))

/// Converts integer input to string.
class IntToText : public BaseNode<IntToText> {
public:
    std::string forward(int input) {
        return std::to_string(input);
    }
    EW_ENABLE_METHODS(forward)
};

EW_REGISTER_NODE(IntToText, "IntToText")

/// Prepends a fixed prefix to string input.
class PrefixText : public BaseNode<PrefixText> {
public:
    explicit PrefixText(std::string prefix) : prefix_(std::move(prefix)) {}

    std::string forward(std::string input) {
        return prefix_ + input;
    }
    EW_ENABLE_METHODS(forward)

private:
    std::string prefix_;
};

EW_REGISTER_NODE(PrefixText, "PrefixText", Arg("prefix", std::string("[Prefix] ")))

/// Emits a tuple of (int, string) sequence.
class PairEmitter : public BaseNode<PairEmitter> {
public:
    PairEmitter(int start, int max)
        : current_(start), max_(max) {
            RegisterTupleType<std::tuple<int, std::string>>();
        }

    std::tuple<int, std::string> forward() {
        if (current_ > max_) {
            Stop();
            return {0, ""};
        }
        int value = current_;
        if (current_ >= max_) {
            Stop();
        }
        current_++;
        return {value, "value_" + std::to_string(value)};
    }
    EW_ENABLE_METHODS(forward)

private:
    int current_;
    int max_;
};

EW_REGISTER_NODE(PairEmitter, "PairEmitter",
    Arg("start", 0),
    Arg("max", 5)
)

/// Joins an integer and a string into a formatted string.
class PairJoiner : public BaseNode<PairJoiner> {
public:
    std::string forward(int number, std::string text) {
        return text + ":" + std::to_string(number);
    }
    EW_ENABLE_METHODS(forward)
};

EW_REGISTER_NODE(PairJoiner, "PairJoiner")

/// Helper struct for testing Object Lifecycle and SBO optimizations.
struct SmallTracked {
    int value;
    static inline std::atomic<int> live_count{0};

    explicit SmallTracked(int v = 0) : value(v) { ++live_count; }
    SmallTracked(const SmallTracked& other) : value(other.value) { ++live_count; }
    SmallTracked(SmallTracked&& other) noexcept : value(other.value) { ++live_count; }
    ~SmallTracked() { --live_count; }
};

inline int GetSmallTrackedLiveCount() { return SmallTracked::live_count.load(); }
inline void ResetSmallTrackedLiveCount() { SmallTracked::live_count.store(0); }

class SmallTrackedSource : public BaseNode<SmallTrackedSource> {
public:
    explicit SmallTrackedSource(int max) : current_(0), max_(max) {}

    SmallTracked forward() {
        if (current_ >= max_) {
            Stop();
            return SmallTracked(0);
        }
        return SmallTracked(current_++);
    }
    EW_ENABLE_METHODS(forward)

private:
    int current_;
    int max_;
};

EW_REGISTER_NODE(SmallTrackedSource, "SmallTrackedSource", Arg("max", 3))

class SmallTrackedConsumer : public BaseNode<SmallTrackedConsumer> {
public:
    int forward(SmallTracked input) {
        return input.value;
    }
    EW_ENABLE_METHODS(forward)
};

EW_REGISTER_NODE(SmallTrackedConsumer, "SmallTrackedConsumer")

/// Test node for verifying method dispatch ordering logic (left/right/forward).
class MethodDispatchRecorder : public BaseNode<MethodDispatchRecorder> {
public:
    MethodDispatchRecorder() = default;

    int forward(int input) {
        // If left or right were not called before forward (when they should have been),
        // we count it as an order error (assuming the test sets up data for all ports).
        if (!left_ready_ || !right_ready_) {
            ++order_error_count;
        }
        left_ready_ = false;
        right_ready_ = false;
        ++forward_count;
        return input;
    }

    int left(int input) {
        left_ready_ = true;
        ++left_count;
        return input;
    }

    int right(int input) {
        right_ready_ = true;
        ++right_count;
        return input;
    }

    // Export all methods
    EW_ENABLE_METHODS(forward, left, right)

    static inline std::atomic<int> left_count{0};
    static inline std::atomic<int> right_count{0};
    static inline std::atomic<int> forward_count{0};
    static inline std::atomic<int> order_error_count{0};

private:
    bool left_ready_{false};
    bool right_ready_{false};
};

inline int GetMethodDispatchLeftCount() {
    return MethodDispatchRecorder::left_count.load();
}

inline int GetMethodDispatchRightCount() {
    return MethodDispatchRecorder::right_count.load();
}

inline int GetMethodDispatchForwardCount() {
    return MethodDispatchRecorder::forward_count.load();
}

inline int GetMethodDispatchOrderErrorCount() {
    return MethodDispatchRecorder::order_error_count.load();
}

inline void ResetMethodDispatchCounts() {
    MethodDispatchRecorder::left_count.store(0);
    MethodDispatchRecorder::right_count.store(0);
    MethodDispatchRecorder::forward_count.store(0);
    MethodDispatchRecorder::order_error_count.store(0);
}

EW_REGISTER_NODE(MethodDispatchRecorder, "MethodDispatchRecorder")

class MixedNode : public BaseNode<MixedNode> {
public:
    MixedNode() = default;

    // Method 1: Data processing (int -> int)
    int forward(int i) { 
        return i + length_; 
    }

    // Method 2: Configuration (string -> void)
    void set_string(std::string s) {
        length_ = static_cast<int>(s.length());
    }

    // Method 3: Computation (int, int -> double) - changing float to double to match Python float
    double compute_ratio(int a, int b) {
        if (b == 0) return 0.0;
        return static_cast<double>(a) / b;
    }

    EW_ENABLE_METHODS(forward, set_string, compute_ratio)

private:
    int length_{0};
};

EW_REGISTER_NODE(MixedNode, "MixedNode")

} // namespace easywork
