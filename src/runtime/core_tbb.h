#pragma once

#include <tbb/flow_graph.h>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <optional>
#include <tuple>
#include <stdexcept>
#include "memory/frame.h"
#include "type_system.h"

namespace easywork {

// The Graph Container
class ExecutionGraph {
public:
    tbb::flow::graph tbb_graph;

    void Reset() {
        tbb_graph.reset();
    }
};

// Base class for all nodes
class Node {
public:
    virtual ~Node() = default;
    virtual void build(ExecutionGraph& g) = 0;

    // Phase 2 Fix: Deferred connection.
    // Store upstream nodes here, and connect in connect() phase.
    std::vector<Node*> upstreams_;

    // 新增：支持带索引的上游连接（用于 tuple 解包）
    struct UpstreamInfo {
        Node* node;
        std::optional<size_t> tuple_index;  // None 或索引值

        UpstreamInfo(Node* n, std::optional<size_t> idx = std::nullopt)
            : node(n), tuple_index(idx) {}
    };

    std::vector<UpstreamInfo> upstreams_info_;

    void add_upstream(Node* n) {
        upstreams_.push_back(n);
        upstreams_info_.emplace_back(n, std::nullopt);
    }

    void add_upstream_with_index(Node* n, std::optional<size_t> index) {
        upstreams_.push_back(n);
        upstreams_info_.emplace_back(n, index);
    }

    // Must be called AFTER build()
    virtual void connect() = 0;

    // Optional activation hook (e.g., input_node).
    virtual void Activate() {}

    // Virtual method for setting input (default: add to upstreams)
    virtual void set_input(Node* upstream) {
        add_upstream(upstream);
    }

    // 新增：获取类型信息（纯虚函数）
    virtual NodeTypeInfo get_type_info() const = 0;
};

// ========== TypedInputNode (Source) ==========
// 模板化的输入节点，支持任意输出类型
template<typename Derived, typename OutputT>
class TypedInputNode : public Node {
public:
    using OutputType = OutputT;
    std::unique_ptr<tbb::flow::input_node<Value>> tbb_node;

    void build(ExecutionGraph& g) override {
        tbb_node = std::make_unique<tbb::flow::input_node<Value>>(
            g.tbb_graph,
            [this](tbb::flow_control& fc) -> Value {
                // 调用派生类的 forward()
                auto result = static_cast<Derived*>(this)->forward(&fc);

                // 如果返回 nullptr（针对 Frame 类型），终止流
                if constexpr (std::is_same_v<OutputT, Frame>) {
                    if (!result) {
                        fc.stop();
                        return Value(nullptr);
                    }
                }

                return Value(std::move(result));
            }
        );
    }

    // 派生类必须实现：OutputT forward(tbb::flow_control* fc)
    // FlowControl* fc 用于终止流（可选）

    NodeTypeInfo get_type_info() const override {
        return NodeTypeInfo{{}, {TypeInfo::create<OutputT>()}};
    }

    void connect() override {
        // InputNode 没有上游连接
    }

    void Activate() override {
        if (tbb_node) {
            tbb_node->activate();
        }
    }
};

// ========== TypedFunctionNode (Process + Sink Unified) ==========
// 模板化的函数节点，支持任意输入输出类型
template<typename Derived, typename InputT, typename OutputT>
class TypedFunctionNode : public Node {
public:
    using InputType = InputT;
    using OutputType = OutputT;
    std::unique_ptr<tbb::flow::function_node<Value, Value>> tbb_node;

    void build(ExecutionGraph& g) override {
        tbb_node = std::make_unique<tbb::flow::function_node<Value, Value>>(
            g.tbb_graph,
            tbb::flow::serial,
            [this](Value val) -> Value {
                try {
                    // 将 Value 转换为输入类型
                    auto input = val.cast<InputT>();

                    // 调用派生类的 forward()
                    auto result = static_cast<Derived*>(this)->forward(input);

                    return Value(std::move(result));
                } catch (const std::exception& e) {
                    // 类型转换或执行失败
                    // 返回空 Value 或抛出异常
                    return Value();
                }
            }
        );
    }

    // 派生类必须实现：OutputT forward(InputT input)

    NodeTypeInfo get_type_info() const override {
        return NodeTypeInfo{
            {TypeInfo::create<InputT>()},
            {TypeInfo::create<OutputT>()}
        };
    }

    void set_input(Node* upstream) override {
        add_upstream(upstream);
    }

    void connect() override {
        for (auto* upstream : upstreams_) {
            // 尝试连接到上游节点
            // 这里简化处理，实际需要更复杂的逻辑来处理不同类型的节点
            try_connect_to(upstream);
        }
    }

    // 尝试连接到上游节点
    void try_connect_to(Node* upstream) {
        // 注意：这里需要知道上游节点的具体类型才能连接
        // 暂时使用 dynamic_cast（不够优雅，但可用）
        // 更好的方案是在 Node 基类中添加虚拟的 connect_to 方法

        // 对于 Frame 类型的旧节点
        if (auto input = dynamic_cast<TypedInputNode<class InputNodeTag, Frame>*>(upstream)) {
            if (input->tbb_node && tbb_node) {
                tbb::flow::make_edge(*(input->tbb_node), *(this->tbb_node));
            }
        } else if (auto func = dynamic_cast<TypedFunctionNode<class FunctionNodeTag, Frame, Frame>*>(upstream)) {
            if (func->tbb_node && tbb_node) {
                tbb::flow::make_edge(*(func->tbb_node), *(this->tbb_node));
            }
        }
        // TODO: 添加其他类型的连接逻辑
    }
};

// ========== TypedMultiInputFunctionNode ==========
// 多输入函数节点，支持 forward(Input1, Input2, ...)
template<typename Derived, typename OutputT, typename... InputTs>
class TypedMultiInputFunctionNode : public Node {
public:
    using InputTuple = std::tuple<InputTs...>;
    using OutputType = OutputT;
    std::unique_ptr<tbb::flow::function_node<Value, Value>> tbb_node;

    void build(ExecutionGraph& g) override {
        tbb_node = std::make_unique<tbb::flow::function_node<Value, Value>>(
            g.tbb_graph,
            tbb::flow::serial,
            [this](Value val) -> Value {
                try {
                    // 将 Value 转换为输入 tuple
                    auto input_tuple = val.cast<InputTuple>();

                    // 解包 tuple 并调用派生类的 forward()
                    auto result = std::apply([this](InputTs... args) {
                        return static_cast<Derived*>(this)->forward(args...);
                    }, input_tuple);

                    return Value(std::move(result));
                } catch (const std::exception& e) {
                    return Value();
                }
            }
        );
    }

    // 派生类必须实现：OutputT forward(InputTs... inputs)

    NodeTypeInfo get_type_info() const override {
        return NodeTypeInfo{
            {TypeInfo::create<InputTs>()...},  // 多个输入类型
            {TypeInfo::create<OutputT>()}
        };
    }

    void set_input(Node* upstream) override {
        add_upstream(upstream);
    }

    void connect() override {
        // 多输入节点：需要将所有上游打包成一个 tuple
        // 这里需要使用 TupleJoinNode（稍后实现）
        if (upstreams_.size() != sizeof...(InputTs)) {
            throw std::runtime_error(
                "Expected " + std::to_string(sizeof...(InputTs)) + " inputs, got " +
                std::to_string(upstreams_.size())
            );
        }

        // TODO: 实现 TupleJoinNode 的连接逻辑
        // 暂时使用简化的连接方式
        for (auto* upstream : upstreams_) {
            try_connect_to(upstream);
        }
    }

    void try_connect_to(Node* upstream) {
        // 暂时使用简化的连接逻辑
        if (auto func = dynamic_cast<TypedFunctionNode<class FunctionNodeTag, Frame, Frame>*>(upstream)) {
            if (func->tbb_node && tbb_node) {
                tbb::flow::make_edge(*(func->tbb_node), *(this->tbb_node));
            }
        }
    }
};

// ========== TupleGetNode ==========
// Tuple 自动索引节点（内部使用）
template<size_t Index, typename TupleT>
class TupleGetNode : public TypedFunctionNode<
    TupleGetNode<Index, TupleT>,
    TupleT,
    std::tuple_element_t<Index, TupleT>> {
public:
    using ElementType = std::tuple_element_t<Index, TupleT>;

    ElementType forward(TupleT input) {
        return std::get<Index>(input);
    }
};

// ========== 向后兼容的 InputNode 和 FunctionNode ==========
// 为了让现有代码继续工作，我们需要实际的类而不是别名
// 这些类将作为新模板化基类的包装器

class InputNode : public Node {
public:
    using OutputType = Frame;
    std::unique_ptr<tbb::flow::input_node<Value>> tbb_node;

    void build(ExecutionGraph& g) override {
        tbb_node = std::make_unique<tbb::flow::input_node<Value>>(
            g.tbb_graph,
            [this](tbb::flow_control& fc) -> Value {
                Frame f = this->forward(&fc);
                if (!f) {
                    fc.stop();
                    return Value(nullptr);
                }
                return Value(f);
            }
        );
    }

    // 派生类必须实现这个方法
    virtual Frame forward(tbb::flow_control* fc = nullptr) = 0;

    NodeTypeInfo get_type_info() const override {
        return NodeTypeInfo{{}, {TypeInfo::create<Frame>()}};
    }

    void connect() override {}

    void Activate() override {
        if (tbb_node) {
            tbb_node->activate();
        }
    }
};

class FunctionNode : public Node {
public:
    std::unique_ptr<tbb::flow::function_node<Value, Value>> tbb_node;

    void build(ExecutionGraph& g) override {
        tbb_node = std::make_unique<tbb::flow::function_node<Value, Value>>(
            g.tbb_graph,
            tbb::flow::serial,
            [this](Value val) -> Value {
                if (!val.has_value()) return Value(nullptr);
                try {
                    Frame f = val.cast<Frame>();
                    Frame result = this->forward(f);
                    return result ? Value(result) : Value(nullptr);
                } catch (...) {
                    return Value(nullptr);
                }
            }
        );
    }

    // 派生类必须实现这个方法
    virtual Frame forward(Frame input) = 0;

    NodeTypeInfo get_type_info() const override {
        return NodeTypeInfo{
            {TypeInfo::create<Frame>()},
            {TypeInfo::create<Frame>()}
        };
    }

    void set_input(Node* upstream) override {
        add_upstream(upstream);
    }

    void connect() override {
        for (auto* upstream : upstreams_) {
            // 尝试连接到 InputNode
            if (auto input = dynamic_cast<InputNode*>(upstream)) {
                if (input->tbb_node && tbb_node) {
                    tbb::flow::make_edge(*(input->tbb_node), *(this->tbb_node));
                }
            } else if (auto func = dynamic_cast<FunctionNode*>(upstream)) {
                if (func->tbb_node && tbb_node) {
                    tbb::flow::make_edge(*(func->tbb_node), *(this->tbb_node));
                }
            }
        }
    }
};

// --- The Executor ---
class Executor {
public:
    void run(ExecutionGraph& g) {
        // In OneTBB, wait_for_all() is the standard way to wait for graph completion.
        g.tbb_graph.wait_for_all();
    }
};

} // namespace easywork
