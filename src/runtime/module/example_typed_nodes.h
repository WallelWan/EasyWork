#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"
#include <iostream>
#include <atomic>
#include <string>
#include <tuple>

namespace easywork {

// 示例：整数计数器节点
class IntCounter : public TypedInputNode<IntCounter, int> {
public:
    IntCounter(int start, int max, int step)
        : current_(start), max_(max), step_(step) {
        spdlog::info("IntCounter created: start={}, max={}, step={}",
                     start, max, step);
    }

    int forward(tbb::flow_control* fc) {
        if (current_ > max_) {
            spdlog::debug("IntCounter: stopping at {}", current_);
            if (fc) fc->stop();
            return 0;
        }
        int val = current_;
        current_ += step_;
        return val;
    }

private:
    int current_;
    int max_;
    int step_;
};

EW_REGISTER_NODE(IntCounter, "IntCounter",
    Arg("start", 0),
    Arg("max", 100),
    Arg("step", 1)
)

// 示例：整数乘法节点
class IntMultiplier : public TypedFunctionNode<IntMultiplier, int, int> {
public:
    explicit IntMultiplier(int factor) : factor_(factor) {
        spdlog::info("IntMultiplier created: factor={}", factor);
    }

    int forward(int input) {
        return input * factor_;
    }

private:
    int factor_;
};

EW_REGISTER_NODE(IntMultiplier, "IntMultiplier", Arg("factor", 2))

// 示例：字符串处理器节点
class StringPrinter : public TypedFunctionNode<StringPrinter, std::string, std::string> {
public:
    std::string forward(std::string input) {
        std::string result = "[StringPrinter] " + input;
        spdlog::info("{}", result);
        return result;
    }
};

EW_REGISTER_NODE(StringPrinter, "StringPrinter")

// 示例：输出 tuple 的节点
class TupleEmitter : public TypedInputNode<TupleEmitter, std::tuple<int, std::string>> {
public:
    TupleEmitter(int start, int max, int step)
        : current_(start), max_(max), step_(step) {}

    std::tuple<int, std::string> forward(tbb::flow_control* fc) {
        if (current_ > max_) {
            if (fc) fc->stop();
            return {0, ""};
        }
        int value = current_;
        current_ += step_;
        return {value, "value_" + std::to_string(value)};
    }

private:
    int current_;
    int max_;
    int step_;
    // 自动注册已由基类构造函数处理，此处不再需要 kTupleRegistered
};

EW_REGISTER_NODE(TupleEmitter, "TupleEmitter",
    Arg("start", 0),
    Arg("max", 5),
    Arg("step", 1)
)

// 示例：多输入节点
class IntStringJoiner : public TypedMultiInputFunctionNode<IntStringJoiner, std::string, int, std::string> {
public:
    std::string forward(int number, std::string text) {
        return text + ":" + std::to_string(number);
    }
};

EW_REGISTER_NODE(IntStringJoiner, "IntStringJoiner")

class IntAdder : public TypedMultiInputFunctionNode<IntAdder, int, int, int> {
public:
    int forward(int left, int right) {
        return left + right;
    }
};

EW_REGISTER_NODE(IntAdder, "IntAdder")

class IntToString : public TypedFunctionNode<IntToString, int, std::string> {
public:
    std::string forward(int input) {
        return std::to_string(input);
    }
};

EW_REGISTER_NODE(IntToString, "IntToString")

// Small buffer safety test
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

class SmallTrackedSource : public TypedInputNode<SmallTrackedSource, SmallTracked> {
public:
    explicit SmallTrackedSource(int max) : current_(0), max_(max) {}
    SmallTracked forward(tbb::flow_control* fc) {
        if (current_ >= max_) {
            if (fc) fc->stop();
            return SmallTracked(0);
        }
        return SmallTracked(current_++);
    }
private:
    int current_;
    int max_;
};

EW_REGISTER_NODE(SmallTrackedSource, "SmallTrackedSource", Arg("max", 3))

class SmallTrackedConsumer : public TypedFunctionNode<SmallTrackedConsumer, SmallTracked, int> {
public:
    int forward(SmallTracked input) {
        return input.value;
    }
};

EW_REGISTER_NODE(SmallTrackedConsumer, "SmallTrackedConsumer")

} // namespace easywork
