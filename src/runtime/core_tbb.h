#pragma once

#include <tbb/flow_graph.h>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <tuple>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <utility>
#include <unordered_map>
#include <mutex>
#include <functional>
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

    void add_upstream(Node* n) {
        upstreams_.push_back(n);
    }

    void ClearUpstreams() {
        upstreams_.clear();
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

    virtual tbb::flow::sender<Value>* get_sender() { return nullptr; }
    virtual tbb::flow::receiver<Value>* get_receiver() { return nullptr; }

    const std::vector<Node*>& get_upstreams() const { return upstreams_; }
};

namespace detail {
// Helper trait to check if a type is std::tuple
template <typename T> struct is_tuple_impl : std::false_type {};
template <typename... Ts> struct is_tuple_impl<std::tuple<Ts...>> : std::true_type {};
template <typename T> struct is_tuple : is_tuple_impl<std::decay_t<T>> {};
template <typename T> inline constexpr bool is_tuple_v = is_tuple<T>::value;
} // namespace detail

// Forward declaration
template<typename TupleT>
bool RegisterTupleType();

// ========== TypedInputNode (Source) ==========
// 模板化的输入节点，支持任意输出类型
template<typename Derived, typename OutputT>
class TypedInputNode : public Node {
public:
    TypedInputNode() {
        if constexpr (detail::is_tuple_v<OutputT>) {
            static const bool _registered = RegisterTupleType<OutputT>();
            (void)_registered;
        }
    }

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

    tbb::flow::sender<Value>* get_sender() override {
        return tbb_node.get();
    }
};

// ========== TypedFunctionNode (Process + Sink Unified) ==========
// 模板化的函数节点，支持任意输入输出类型
template<typename Derived, typename InputT, typename OutputT>
class TypedFunctionNode : public Node {
public:
    TypedFunctionNode() {
        if constexpr (detail::is_tuple_v<OutputT>) {
            static const bool _registered = RegisterTupleType<OutputT>();
            (void)_registered;
        }
    }

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
            auto* sender = upstream ? upstream->get_sender() : nullptr;
            auto* receiver = get_receiver();
            if (sender && receiver) {
                tbb::flow::make_edge(*sender, *receiver);
            } else {
                throw std::runtime_error("Failed to connect: missing sender/receiver");
            }
        }
    }

    tbb::flow::sender<Value>* get_sender() override {
        return tbb_node.get();
    }

    tbb::flow::receiver<Value>* get_receiver() override {
        return tbb_node.get();
    }
};

// ========== TypedMultiInputFunctionNode ==========
// 多输入函数节点，支持 forward(Input1, Input2, ...)
template<typename Derived, typename OutputT, typename... InputTs>
class TypedMultiInputFunctionNode : public Node {
public:
    using OutputType = OutputT;
    using JoinTuple = std::tuple<std::conditional_t<true, Value, InputTs>...>;
    std::unique_ptr<tbb::flow::join_node<JoinTuple, tbb::flow::queueing>> join_node_;
    std::unique_ptr<tbb::flow::function_node<JoinTuple, Value>> tbb_node;

    void build(ExecutionGraph& g) override {
        join_node_ = std::make_unique<tbb::flow::join_node<JoinTuple, tbb::flow::queueing>>(
            g.tbb_graph);
        tbb_node = std::make_unique<tbb::flow::function_node<JoinTuple, Value>>(
            g.tbb_graph,
            tbb::flow::serial,
            [this](const JoinTuple& vals) -> Value {
                try {
                    auto result = InvokeWithValues(vals,
                                                   std::index_sequence_for<InputTs...>{});
                    return Value(std::move(result));
                } catch (const std::exception& e) {
                    return Value();
                }
            });
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
        if (upstreams_.size() != sizeof...(InputTs)) {
            throw std::runtime_error(
                "Expected " + std::to_string(sizeof...(InputTs)) + " inputs, got " +
                std::to_string(upstreams_.size())
            );
        }

        if (!join_node_ || !tbb_node) {
            throw std::runtime_error("Join node is not initialized");
        }

        ConnectInputs(std::index_sequence_for<InputTs...>{});
        tbb::flow::make_edge(*join_node_, *tbb_node);
    }

    tbb::flow::sender<Value>* get_sender() override {
        return tbb_node.get();
    }

private:
    template<size_t... Index>
    void ConnectInputs(std::index_sequence<Index...>) {
        (ConnectInput<Index>(upstreams_[Index]), ...);
    }

    template<size_t Index>
    void ConnectInput(Node* upstream) {
        auto* sender = upstream ? upstream->get_sender() : nullptr;
        if (!sender) {
            throw std::runtime_error("Failed to connect multi-input: missing sender");
        }
        tbb::flow::make_edge(*sender, tbb::flow::input_port<Index>(*join_node_));
    }

    template<size_t... Index>
    OutputT InvokeWithValues(const JoinTuple& vals, std::index_sequence<Index...>) {
        return static_cast<Derived*>(this)->forward(
            std::get<Index>(vals).template cast<InputTs>()...);
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

namespace detail {

struct TupleRegistryEntry {
    size_t size;
    std::function<std::shared_ptr<Node>(size_t)> factory;
};

inline std::unordered_map<size_t, TupleRegistryEntry>& TupleRegistry() {
    static std::unordered_map<size_t, TupleRegistryEntry> registry;
    return registry;
}

inline std::mutex& TupleRegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

template<typename TupleT, size_t... Index>
std::shared_ptr<Node> CreateTupleGetNodeForIndex(size_t index,
                                                 std::index_sequence<Index...>) {
    std::shared_ptr<Node> node;
    bool matched = ((index == Index
                         ? (node = std::make_shared<TupleGetNode<Index, TupleT>>(), true)
                         : false) || ...);
    (void)matched;
    if (!node) {
        throw std::runtime_error("Unsupported tuple index for TupleGetNode");
    }
    return node;
}
}  // namespace detail

template<typename TupleT>
inline bool RegisterTupleType() {
    static_assert(std::tuple_size_v<TupleT> > 0, "Tuple type must not be empty");
    const auto type_info = TypeInfo::create<TupleT>();
    const auto type_key = type_info.type_hash;
    std::lock_guard<std::mutex> lock(detail::TupleRegistryMutex());
    auto& registry = detail::TupleRegistry();
    if (registry.contains(type_key)) {
        return false;
    }
    detail::TupleRegistryEntry entry;
    entry.size = std::tuple_size_v<TupleT>;
    entry.factory = [](size_t index) {
        return detail::CreateTupleGetNodeForIndex<TupleT>(
            index, std::make_index_sequence<std::tuple_size_v<TupleT>>{});
    };
    registry.emplace(type_key, std::move(entry));
    return true;
}

inline std::shared_ptr<Node> CreateTupleGetNode(const TypeInfo& tuple_type, size_t index) {
    std::lock_guard<std::mutex> lock(detail::TupleRegistryMutex());
    auto& registry = detail::TupleRegistry();
    auto it = registry.find(tuple_type.type_hash);
    if (it == registry.end()) {
        throw std::runtime_error("Tuple type not registered for TupleGetNode");
    }
    if (index >= it->second.size) {
        throw std::runtime_error("Tuple index out of range for TupleGetNode");
    }
    return it->second.factory(index);
}

inline size_t GetTupleSize(const TypeInfo& tuple_type) {
    std::lock_guard<std::mutex> lock(detail::TupleRegistryMutex());
    auto& registry = detail::TupleRegistry();
    auto it = registry.find(tuple_type.type_hash);
    if (it == registry.end()) {
        return 0;
    }
    return it->second.size;
}

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

    tbb::flow::sender<Value>* get_sender() override {
        return tbb_node.get();
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
            auto* sender = upstream ? upstream->get_sender() : nullptr;
            auto* receiver = get_receiver();
            if (sender && receiver) {
                tbb::flow::make_edge(*sender, *receiver);
            } else {
                throw std::runtime_error("Failed to connect: missing sender/receiver");
            }
        }
    }

    tbb::flow::sender<Value>* get_sender() override {
        return tbb_node.get();
    }

    tbb::flow::receiver<Value>* get_receiver() override {
        return tbb_node.get();
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
