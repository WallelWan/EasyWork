#pragma once

#include <iostream>
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

// ========== TypedMultiInputFunctionNode ==========
// Comprehensive base class handling both Sources (0 inputs) and Functions (N inputs)
template<typename Derived, typename OutputT, typename... InputTs>
class TypedMultiInputFunctionNode : public Node {
public:
    using OutputType = OutputT;
    using Self = Derived;
    using MethodSignature = OutputT (Derived::*)(InputTs...);

    static constexpr bool IsSource = (sizeof...(InputTs) == 0);

    // Types for Function logic (N inputs)
    using JoinTuple = std::tuple<std::conditional_t<true, Value, InputTs>...>;
    
    // Conditional member types
    using InputNodeT = tbb::flow::input_node<Value>;
    using JoinNodeT = std::conditional_t<IsSource, 
        tbb::flow::join_node<std::tuple<Value>, tbb::flow::queueing>, // Dummy type
        tbb::flow::join_node<JoinTuple, tbb::flow::queueing>>;
    using FuncNodeT = std::conditional_t<IsSource,
        tbb::flow::function_node<std::tuple<Value>, Value>, // Dummy type
        tbb::flow::function_node<JoinTuple, Value>>;

    // Members
    // For Source
    std::unique_ptr<InputNodeT> input_node_;
    // For Function
    std::unique_ptr<JoinNodeT> join_node_;
    std::unique_ptr<FuncNodeT> func_node_;
    
    tbb::flow::graph* graph_ = nullptr;
    std::vector<std::unique_ptr<tbb::flow::function_node<Value, Value>>> tagger_nodes_;

    TypedMultiInputFunctionNode() {
        if constexpr (detail::is_tuple_v<OutputT>) {
            static const bool _registered = RegisterTupleType<OutputT>();
            (void)_registered;
        }
    }

    void build(ExecutionGraph& g) override {
        graph_ = &g.tbb_graph;
        
        if constexpr (IsSource) {
            input_node_ = std::make_unique<InputNodeT>(
                g.tbb_graph,
                [this](tbb::flow_control& fc) -> Value {
                    // Call Derived::forward(tbb::flow_control*)
                    auto result = static_cast<Derived*>(this)->forward(&fc);

                    // Special handling for Frame: stop if null
                    if constexpr (std::is_same_v<OutputT, Frame>) {
                        if (!result) {
                            fc.stop();
                            return Value(nullptr);
                        }
                    }
                    return Value(std::move(result));
                }
            );
        } else {
            join_node_ = std::make_unique<JoinNodeT>(g.tbb_graph);
            func_node_ = std::make_unique<FuncNodeT>(
                g.tbb_graph,
                tbb::flow::serial,
                [this](const JoinTuple& vals) -> Value {
                    try {
                        auto result = InvokeWithValues(vals,
                                                     std::index_sequence_for<InputTs...>{});
                        return Value(std::move(result));
                    } catch (const std::exception& e) {
                        return Value(); // Or handle error
                    }
                }
            );
        }
    }

    NodeTypeInfo get_type_info() const override {
        return NodeTypeInfo{
            {TypeInfo::create<InputTs>()...},
            {TypeInfo::create<OutputT>()}
        };
    }

    void set_input(Node* upstream) override {
        if constexpr (IsSource) {
             throw std::runtime_error("Cannot set input on a Source node (TypedMultiInputFunctionNode with 0 inputs)");
        }
        add_upstream(upstream);
    }

    void connect() override {
        if constexpr (IsSource) {
            // Sources don't have upstreams
        } else {
            if (!join_node_ || !func_node_) {
                throw std::runtime_error("Internal nodes not initialized");
            }

            if constexpr (sizeof...(InputTs) == 1) {
                // Single input: Allow multiple upstreams (multiplexing)
                if (upstreams_.empty()) {
                    throw std::runtime_error("Expected at least 1 input, got 0");
                }
                for (size_t i = 0; i < upstreams_.size(); ++i) {
                    ConnectInput<0>(upstreams_[i], upstream_methods_[i]);
                }
            } else {
                // Multi-input: Strict 1:1 mapping
                if (upstreams_.size() != sizeof...(InputTs)) {
                    throw std::runtime_error(
                        "Expected " + std::to_string(sizeof...(InputTs)) + " inputs, got " +
                        std::to_string(upstreams_.size())
                    );
                }
                ConnectInputs(std::index_sequence_for<InputTs...>{});
            }

            tbb::flow::make_edge(*join_node_, *func_node_);
        }
    }

    void Activate() override {
        if constexpr (IsSource) {
            if (input_node_) {
                input_node_->activate();
            }
        }
    }

    tbb::flow::sender<Value>* get_sender() override {
        if constexpr (IsSource) {
            return input_node_.get();
        } else {
            return func_node_.get();
        }
    }

    tbb::flow::receiver<Value>* get_receiver() override {
        return nullptr;
    }

private:
    // Helper methods for Function Logic
    template<size_t... Index>
    void ConnectInputs(std::index_sequence<Index...>) {
        (ConnectInput<Index>(upstreams_[Index], upstream_methods_[Index]), ...);
    }

    template<size_t Index>
    void ConnectInput(Node* upstream, const std::string& method) {
        if (!upstream) {
             std::cerr << "TypedMultiInputFunctionNode::ConnectInput: upstream is null" << std::endl;
             return;
        }
        auto* sender = upstream->get_sender();
        if (!sender) {
            std::cerr << "TypedMultiInputFunctionNode::ConnectInput: sender is null" << std::endl;
            throw std::runtime_error("Failed to connect: missing sender");
        }
        
        if (!method.empty()) {
            auto tagger = std::make_unique<tbb::flow::function_node<Value, Value>>(
                *graph_,
                tbb::flow::serial,
                [method](Value val) -> Value {
                    return Value(MethodTaggedValue{method, std::move(val)});
                });
            tbb::flow::make_edge(*sender, *tagger);
            // Input ports are on join_node_
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
        std::tuple<InputTs...> inputs(UnwrapValue<Index, InputTs>(std::get<Index>(vals), methods)...);
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
};

// ========== TupleGetNode ==========
// Tuple 自动索引节点（内部使用）
template<size_t Index, typename TupleT>
class TupleGetNode : public TypedMultiInputFunctionNode<
    TupleGetNode<Index, TupleT>,
    std::tuple_element_t<Index, TupleT>,
    TupleT> {
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