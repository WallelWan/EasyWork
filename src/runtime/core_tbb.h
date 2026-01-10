#pragma once

#include <tbb/flow_graph.h>
#include <array>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <tuple>
#include <stdexcept>
#include <type_traits>
#include <concepts>
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
    std::vector<std::string> upstream_methods_;

    void add_upstream(Node* n, std::string method = {}) {
        upstreams_.push_back(n);
        upstream_methods_.push_back(std::move(method));
    }

    void ClearUpstreams() {
        upstreams_.clear();
        upstream_methods_.clear();
    }

    // Must be called AFTER build()
    virtual void connect() = 0;

    // Optional activation hook (e.g., input_node).
    virtual void Activate() {}

    // Virtual method for setting input (default: add to upstreams)
    virtual void set_input(Node* upstream) {
        add_upstream(upstream);
    }

    virtual void set_input_for(const std::string& method, Node* upstream) {
        if (method.empty() || method == "forward") {
            set_input(upstream);
            return;
        }
        add_upstream(upstream, method);
    }

    // 新增：获取类型信息（纯虚函数）
    virtual NodeTypeInfo get_type_info() const = 0;

    virtual std::vector<std::string> exposed_methods() const {
        return {"forward"};
    }

    virtual tbb::flow::sender<Value>* get_sender() { return nullptr; }
    virtual tbb::flow::receiver<Value>* get_receiver() { return nullptr; }

    const std::vector<Node*>& get_upstreams() const { return upstreams_; }
};

struct MethodTaggedValue {
    std::string method;
    Value payload;
};

inline bool IsTaggedValue(const Value& value) {
    return value.has_value() && value.type() == TypeInfo::create<MethodTaggedValue>();
}

inline MethodTaggedValue AsTaggedValue(const Value& value) {
    return value.cast<MethodTaggedValue>();
}

namespace detail {
// Helper trait to check if a type is std::tuple
template <typename T> struct is_tuple_impl : std::false_type {};
template <typename... Ts> struct is_tuple_impl<std::tuple<Ts...>> : std::true_type {};
template <typename T> struct is_tuple : is_tuple_impl<std::decay_t<T>> {};
template <typename T> inline constexpr bool is_tuple_v = is_tuple<T>::value;

template<typename Derived, typename MethodTable>
inline constexpr bool has_method_table_v = requires {
    { Derived::method_table() } -> std::same_as<MethodTable>;
};
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
        // Source node 没有上游连接
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
                    std::string method;
                    Value payload = val;
                    if (IsTaggedValue(val)) {
                        auto tagged = AsTaggedValue(val);
                        method = std::move(tagged.method);
                        payload = std::move(tagged.payload);
                    }
                    auto input = payload.cast<InputT>();

                    // 调用派生类的 forward()
                    if (!method.empty() && method != "forward") {
                        if constexpr (detail::has_method_table_v<
                                          Derived,
                                          std::unordered_map<std::string,
                                                             OutputT (Derived::*)(InputT)>>) {
                            static const auto table = Derived::method_table();
                            auto it = table.find(method);
                            if (it != table.end()) {
                                auto fn = it->second;
                                auto result = (static_cast<Derived*>(this)->*fn)(input);
                                return Value(std::move(result));
                            }
                        }
                        throw std::runtime_error("Unknown method: " + method);
                    }
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
        for (size_t index = 0; index < upstreams_.size(); ++index) {
            auto* upstream = upstreams_[index];
            auto* sender = upstream ? upstream->get_sender() : nullptr;
            auto* receiver = get_receiver();
            if (sender && receiver) {
                const auto& method = upstream_methods_[index];
                if (!method.empty()) {
                    auto tagger = std::make_unique<tbb::flow::function_node<Value, Value>>(
                        tbb_node->graph_reference(),
                        tbb::flow::serial,
                        [method](Value val) -> Value {
                            return Value(MethodTaggedValue{method, std::move(val)});
                        });
                    tbb::flow::make_edge(*sender, *tagger);
                    tbb::flow::make_edge(*tagger, *receiver);
                    tagger_nodes_.push_back(std::move(tagger));
                } else {
                    tbb::flow::make_edge(*sender, *receiver);
                }
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

private:
    std::vector<std::unique_ptr<tbb::flow::function_node<Value, Value>>> tagger_nodes_;
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
        const auto& method = upstream_methods_[Index];
        if (!method.empty()) {
            auto tagger = std::make_unique<tbb::flow::function_node<Value, Value>>(
                join_node_->graph_reference(),
                tbb::flow::serial,
                [method](Value val) -> Value {
                    return Value(MethodTaggedValue{method, std::move(val)});
                });
            tbb::flow::make_edge(*sender, *tagger);
            tbb::flow::make_edge(*tagger, tbb::flow::input_port<Index>(*join_node_));
            tagger_nodes_.push_back(std::move(tagger));
        } else {
            tbb::flow::make_edge(*sender, tbb::flow::input_port<Index>(*join_node_));
        }
    }

    template<size_t... Index>
    OutputT InvokeWithValues(const JoinTuple& vals, std::index_sequence<Index...>) {
        std::array<std::string, sizeof...(InputTs)> methods;
        methods.fill(std::string());
        auto result = InvokeWithValuesAndMethods(vals, methods,
                                                 std::index_sequence<Index...>{});
        return result;
    }

    template<size_t Index, typename InputT>
    InputT UnwrapValue(const Value& value,
                       std::array<std::string, sizeof...(InputTs)>& methods) {
        if (IsTaggedValue(value)) {
            auto tagged = AsTaggedValue(value);
            methods[Index] = tagged.method;
            return tagged.payload.template cast<InputT>();
        }
        return value.template cast<InputT>();
    }

    template<size_t... Index>
    OutputT InvokeWithValuesAndMethods(const JoinTuple& vals,
                                       std::array<std::string, sizeof...(InputTs)>& methods,
                                       std::index_sequence<Index...>) {
        auto inputs = std::tuple{UnwrapValue<Index, InputTs>(std::get<Index>(vals), methods)...};
        bool has_method = !methods.empty() && !methods[0].empty();
        for (const auto& method : methods) {
            if (method != methods[0] || method.empty()) {
                has_method = false;
                break;
            }
        }
        if (has_method && methods[0] != "forward") {
            if constexpr (detail::has_method_table_v<
                              Derived,
                              std::unordered_map<std::string,
                                                 OutputT (Derived::*)(InputTs...)>>) {
                static const auto table = Derived::method_table();
                auto it = table.find(methods[0]);
                if (it != table.end()) {
                    auto fn = it->second;
                    return std::apply(
                        [this, fn](auto&&... args) {
                            return (static_cast<Derived*>(this)->*fn)(
                                std::forward<decltype(args)>(args)...);
                        },
                        inputs);
                }
            }
            throw std::runtime_error("Unknown method: " + methods[0]);
        }
        return std::apply(
            [this](auto&&... args) {
                return static_cast<Derived*>(this)->forward(
                    std::forward<decltype(args)>(args)...);
            },
            inputs);
    }

    std::vector<std::unique_ptr<tbb::flow::function_node<Value, Value>>> tagger_nodes_;
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

// --- The Executor ---
class Executor {
public:
    void run(ExecutionGraph& g) {
        // In OneTBB, wait_for_all() is the standard way to wait for graph completion.
        g.tbb_graph.wait_for_all();
    }
};

} // namespace easywork
