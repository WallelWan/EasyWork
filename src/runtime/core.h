#pragma once

#include <iostream>
#include <taskflow/taskflow.hpp>
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

// ========== Constants ==========

constexpr size_t ID_FORWARD = hash_string("forward");

// ========== Graph Container ==========

class ExecutionGraph {
public:
    tf::Taskflow taskflow;
    tf::Executor executor;
    std::atomic<bool> keep_running{true};

    void Reset() {
        taskflow.clear();
        keep_running = true;
    }
};

// ========== Flow Control ==========

struct FlowControl {
    ExecutionGraph* graph;
    void stop() {
        if (graph) graph->keep_running = false;
    }
};

// ========== Method Tagged Value (Optimized) ==========

struct MethodTaggedValue {
    size_t method_id;
    Value payload;
};

inline bool IsTaggedValue(const Value& value) {
    return value.has_value() && value.type() == TypeInfo::create<MethodTaggedValue>();
}

inline MethodTaggedValue AsTaggedValue(const Value& value) {
    return value.cast<MethodTaggedValue>();
}

// ========== Node Base Class ==========

class Node {
public:
    virtual ~Node() = default;

    // Build the task in the graph
    virtual void build(ExecutionGraph& g) = 0;

    // Connection Logic
    struct UpstreamConnection {
        Node* node;
        size_t method_id;
    };

    struct PortInfo {
        size_t index;
        size_t method_id;
        bool is_control;
    };

    std::vector<UpstreamConnection> upstreams_;
    std::vector<PortInfo> port_map_;
    std::vector<std::string> upstream_methods_debug_; // Keep strings for debug/introspection

    void add_upstream(Node* n, std::string method = {}) {
        size_t id = method.empty() ? ID_FORWARD : hash_string(method);
        size_t index = upstreams_.size();
        upstreams_.push_back({n, id});
        port_map_.push_back({index, id, id != ID_FORWARD});
        upstream_methods_debug_.push_back(std::move(method));
    }

    void ClearUpstreams() {
        upstreams_.clear();
        port_map_.clear();
        upstream_methods_debug_.clear();
    }

    virtual void connect() = 0;

    virtual void Activate() {}

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

    virtual NodeTypeInfo get_type_info() const = 0;

    virtual std::vector<std::string> exposed_methods() const {
        return {"forward"};
    }

    // Taskflow Integration
    tf::Task get_task() const { return task_; }

    const std::vector<Node*>& get_upstreams() const {
        upstream_nodes_.clear();
        upstream_nodes_.reserve(upstreams_.size());
        for (const auto& conn : upstreams_) {
            if (conn.node) {
                upstream_nodes_.push_back(conn.node);
            }
        }
        return upstream_nodes_;
    }
    
    // Output Storage
    Value output_value_;

protected:
    tf::Task task_;

private:
    mutable std::vector<Node*> upstream_nodes_;
};

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

// ========== BaseNode ==========

template<typename Derived, typename OutputT, typename... InputTs>
class BaseNode : public Node {
public:
    using OutputType = OutputT;
    using Self = Derived;
    using MethodSignature = OutputT (Derived::*)(InputTs...);

    static constexpr bool IsSource = (sizeof...(InputTs) == 0);
    using JoinTuple = std::tuple<std::conditional_t<true, Value, InputTs>...>;

    BaseNode() {
        if constexpr (detail::is_tuple_v<OutputT>) {
            static const bool _registered = RegisterTupleType<OutputT>();
            (void)_registered;
        }
    }

    void build(ExecutionGraph& g) override {
        if constexpr (IsSource) {
            task_ = g.taskflow.emplace([this, &g]() {
                // Source Logic
                if (!g.keep_running) return; // Stop if already signaled

                FlowControl fc{&g};
                
                // Call Derived::forward(FlowControl*)
                auto result = static_cast<Derived*>(this)->forward(&fc);
                
                if constexpr (std::is_same_v<OutputT, Frame>) {
                    if (!result) {
                        g.keep_running = false;
                        output_value_ = Value(nullptr);
                        return;
                    }
                }
                output_value_ = Value(std::move(result));
            });
        } else {
            task_ = g.taskflow.emplace([this]() {
                // Function Logic
                try {
                    Value last_output;
                    bool has_output = false;

                    if constexpr (sizeof...(InputTs) == 1) {
                        using InputT = std::tuple_element_t<0, std::tuple<InputTs...>>;
                        std::vector<Value> forward_inputs;
                        forward_inputs.reserve(upstreams_.size());

                        for (const auto& conn : upstreams_) {
                            if (!conn.node) {
                                continue;
                            }
                            Value input = conn.node->output_value_;
                            if (conn.method_id != ID_FORWARD) {
                                auto result = InvokeSingleInput(conn.method_id, input);
                                last_output = Value(std::move(result));
                                has_output = true;
                            } else {
                                forward_inputs.push_back(std::move(input));
                            }
                        }

                        for (const auto& input : forward_inputs) {
                            auto result = InvokeSingleInput(ID_FORWARD, input);
                            last_output = Value(std::move(result));
                            has_output = true;
                        }
                    } else {
                        JoinTuple inputs = GatherInputs(std::index_sequence_for<InputTs...>{});
                        auto typed_inputs = CastInputs(inputs, std::index_sequence_for<InputTs...>{});

                        std::array<size_t, sizeof...(InputTs)> methods;
                        methods.fill(ID_FORWARD);
                        for (size_t i = 0; i < upstreams_.size() && i < sizeof...(InputTs); ++i) {
                            methods[i] = upstreams_[i].method_id;
                        }

                        bool has_control = false;
                        bool control_consistent = true;
                        size_t control_id = ID_FORWARD;
                        bool has_forward = false;

                        for (size_t i = 0; i < sizeof...(InputTs); ++i) {
                            if (methods[i] == ID_FORWARD) {
                                has_forward = true;
                                continue;
                            }
                            if (!has_control) {
                                control_id = methods[i];
                                has_control = true;
                            } else if (methods[i] != control_id) {
                                control_consistent = false;
                            }
                        }

                        if (has_control && control_consistent) {
                            auto result = InvokeTupleInputs(control_id, typed_inputs);
                            last_output = Value(std::move(result));
                            has_output = true;
                        }

                        if (has_forward) {
                            auto result = InvokeTupleInputs(ID_FORWARD, typed_inputs);
                            last_output = Value(std::move(result));
                            has_output = true;
                        }
                    }

                    output_value_ = has_output ? std::move(last_output) : Value();
                } catch (const std::exception& e) {
                    std::cerr << "Error in node execution: " << e.what() << std::endl;
                    output_value_ = Value(); 
                }
            });
        }
        
        task_.name(TypeInfo::create<Derived>().type_name);
    }

    NodeTypeInfo get_type_info() const override {
        return NodeTypeInfo{
            {TypeInfo::create<InputTs>()...},
            {TypeInfo::create<OutputT>()}
        };
    }

    void connect() override {
        if constexpr (!IsSource) {
            // Check inputs
            if (sizeof...(InputTs) > 0 && upstreams_.empty()) {
                 // Warning or Error?
            }
            
            for (const auto& conn : upstreams_) {
                if (conn.node) {
                    conn.node->get_task().precede(task_);
                }
            }
        }
    }

private:
    template<size_t... Index>
    JoinTuple GatherInputs(std::index_sequence<Index...>) {
        return std::make_tuple(GetInputVal<Index>()...);
    }

    template<size_t Index>
    Value GetInputVal() {
        if (Index >= upstreams_.size()) return Value();
        return upstreams_[Index].node->output_value_;
    }

    template<size_t... Index>
    std::tuple<InputTs...> CastInputs(const JoinTuple& vals, std::index_sequence<Index...>) {
        return std::make_tuple(std::get<Index>(vals).template cast<InputTs>()...);
    }

    OutputT InvokeSingleInput(size_t method_id, const Value& value) {
        using InputT = std::tuple_element_t<0, std::tuple<InputTs...>>;
        InputT input = value.template cast<InputT>();
        return InvokeTupleInputs(method_id, std::tuple<InputT>(std::move(input)));
    }

    template<typename TupleT>
    OutputT InvokeTupleInputs(size_t method_id, TupleT&& inputs) {
        if (method_id != ID_FORWARD) {
            if constexpr (detail::has_method_table_v<
                              Derived,
                              std::unordered_map<size_t,
                                                 OutputT (Derived::*)(InputTs...)>>) {
                static const auto table = Derived::method_table();
                auto it = table.find(method_id);
                if (it != table.end()) {
                    auto fn = it->second;
                    return std::apply(
                        [this, fn](auto&&... args) {
                            return (static_cast<Derived*>(this)->*fn)(
                                std::forward<decltype(args)>(args)...);
                        },
                        std::forward<TupleT>(inputs));
                }
            }
        }

        return std::apply(
            [this](auto&&... args) {
                return static_cast<Derived*>(this)->forward(
                    std::forward<decltype(args)>(args)...);
            },
            std::forward<TupleT>(inputs));
    }
};

// ========== TupleGetNode ==========

template<size_t Index, typename TupleT>
class TupleGetNode : public BaseNode<
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

// ========== Executor ==========

class Executor {
public:
    void run(ExecutionGraph& g) {
        g.keep_running = true;
        // Simple loop driver
        while (g.keep_running) {
             g.executor.run(g.taskflow).wait();
        }
    }
};

} // namespace easywork
