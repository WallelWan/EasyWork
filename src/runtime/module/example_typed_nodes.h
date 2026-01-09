#pragma once

#include "../core_tbb.h"
#include "../node_registry.h"
#include <iostream>

namespace easywork {

// 示例：整数计数器节点（使用新的模板化基类）
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
        spdlog::trace("IntCounter: emitted {}", val);
        return val;
    }

private:
    int current_;
    int max_;
    int step_;
};

// 注册为输入节点
// 注意：这里暂时使用旧的宏，后续可以更新为专门针对类型化节点的宏
EW_REGISTER_NODE_3(IntCounter, "IntCounter",
                  int, start, 0,
                  int, max, 100,
                  int, step, 1)

// 示例：整数乘法节点
class IntMultiplier : public TypedFunctionNode<IntMultiplier, int, int> {
public:
    explicit IntMultiplier(int factor) : factor_(factor) {
        spdlog::info("IntMultiplier created: factor={}", factor);
    }

    int forward(int input) {
        int result = input * factor_;
        spdlog::trace("IntMultiplier: {} -> {}", input, result);
        return result;
    }

private:
    int factor_;
};

EW_REGISTER_NODE_1(IntMultiplier, "IntMultiplier",
                  int, factor, 2)

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

} // namespace easywork
