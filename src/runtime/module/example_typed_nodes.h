#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"
#include "../macros.h"
#include <atomic>
#include <string>
#include <tuple>
#include <unordered_map>

namespace easywork {

class NumberSource : public TypedMultiInputFunctionNode<NumberSource, int> {
public:
    NumberSource(int start, int max, int step)
        : current_(start), max_(max), step_(step) {}

    int forward(tbb::flow_control* fc) {
        if (current_ > max_) {
            if (fc) {
                fc->stop();
            }
            return 0;
        }
        int value = current_;
        current_ += step_;
        return value;
    }

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

class MultiplyBy : public TypedMultiInputFunctionNode<MultiplyBy, int, int> {
public:
    explicit MultiplyBy(int factor) : factor_(factor) {}

    int forward(int input) {
        return input * factor_;
    }

private:
    int factor_;
};

EW_REGISTER_NODE(MultiplyBy, "MultiplyBy", Arg("factor", 2))

class IntToText : public TypedMultiInputFunctionNode<IntToText, std::string, int> {
public:
    std::string forward(int input) {
        return std::to_string(input);
    }
};

EW_REGISTER_NODE(IntToText, "IntToText")

class PrefixText : public TypedMultiInputFunctionNode<PrefixText, std::string, std::string> {
public:
    explicit PrefixText(std::string prefix) : prefix_(std::move(prefix)) {}

    std::string forward(std::string input) {
        return prefix_ + input;
    }

private:
    std::string prefix_;
};

EW_REGISTER_NODE(PrefixText, "PrefixText", Arg("prefix", std::string("[Prefix] ")))

class PairEmitter : public TypedMultiInputFunctionNode<PairEmitter, std::tuple<int, std::string>> {
public:
    PairEmitter(int start, int max)
        : current_(start), max_(max) {}

    std::tuple<int, std::string> forward(tbb::flow_control* fc) {
        if (current_ > max_) {
            if (fc) {
                fc->stop();
            }
            return {0, ""};
        }
        int value = current_++;
        return {value, "value_" + std::to_string(value)};
    }

private:
    int current_;
    int max_;
};

EW_REGISTER_NODE(PairEmitter, "PairEmitter",
    Arg("start", 0),
    Arg("max", 5)
)

class PairJoiner : public TypedMultiInputFunctionNode<PairJoiner, std::string, int, std::string> {
public:
    std::string forward(int number, std::string text) {
        return text + ":" + std::to_string(number);
    }
};

EW_REGISTER_NODE(PairJoiner, "PairJoiner")

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

class SmallTrackedSource : public TypedMultiInputFunctionNode<SmallTrackedSource, SmallTracked> {
public:
    explicit SmallTrackedSource(int max) : current_(0), max_(max) {}

    SmallTracked forward(tbb::flow_control* fc) {
        if (current_ >= max_) {
            if (fc) {
                fc->stop();
            }
            return SmallTracked(0);
        }
        return SmallTracked(current_++);
    }

private:
    int current_;
    int max_;
};

EW_REGISTER_NODE(SmallTrackedSource, "SmallTrackedSource", Arg("max", 3))

class SmallTrackedConsumer : public TypedMultiInputFunctionNode<SmallTrackedConsumer, int, SmallTracked> {
public:
    int forward(SmallTracked input) {
        return input.value;
    }
};

EW_REGISTER_NODE(SmallTrackedConsumer, "SmallTrackedConsumer")

class MethodDispatchRecorder : public TypedMultiInputFunctionNode<MethodDispatchRecorder, int, int> {
public:
    int forward(int input) {
        ++forward_count;
        return input;
    }

    int left(int input) {
        ++left_count;
        return input;
    }

    int right(int input) {
        ++right_count;
        return input;
    }

    EW_EXPORT_METHODS(left, right)

    static inline std::atomic<int> left_count{0};
    static inline std::atomic<int> right_count{0};
    static inline std::atomic<int> forward_count{0};
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

inline void ResetMethodDispatchCounts() {
    MethodDispatchRecorder::left_count.store(0);
    MethodDispatchRecorder::right_count.store(0);
    MethodDispatchRecorder::forward_count.store(0);
}

EW_REGISTER_NODE(MethodDispatchRecorder, "MethodDispatchRecorder")

} // namespace easywork