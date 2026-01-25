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
#include <unordered_set>
#include <mutex>
#include <functional>
#include <deque>
#include <algorithm>
#include <limits>
#include "runtime/memory/frame.h"
#include "runtime/types/type_system.h"
#include "dispatch_unit.h"
#include "runtime/types/type_converter.h"
#include "runtime/registry/macros.h"

namespace easywork {

// ========== Constants ==========

constexpr size_t ID_FORWARD = hash_string("forward");
constexpr size_t ID_OPEN = hash_string("Open");
constexpr size_t ID_CLOSE = hash_string("Close");

// ========== Graph Container ==========

/**
 * @brief Manages the Taskflow execution environment.
 * 
 * Holds the Taskflow graph and the executor. It also manages the global
 * running state of the pipeline.
 */
class ExecutionGraph {
public:
    tf::Taskflow taskflow;
    tf::Executor executor;
    std::atomic<bool> keep_running{true};

    /// Clears the graph and resets the running state.
    void Reset() {
        taskflow.clear();
        keep_running = true;
    }
};

// ========== Flow Control ==========

/**
 * @brief Provides mechanism to stop graph execution from within a node.
 */
struct FlowControl {
    ExecutionGraph* graph;

    /// Signals the execution graph to stop running.
    void stop() {
        if (graph) graph->keep_running = false;
    }
};

// ========== Context ==========

/**
 * @brief Provides execution context for legacy or special nodes.
 * 
 * Allows access to all inputs and management of multiple outputs dynamically.
 * Mostly used for nodes that need direct access to the raw Packet or timestamp data.
 */
class Context {
public:
    Context(std::vector<Packet> inputs, int64_t input_timestamp)
        : inputs_(std::move(inputs)), input_timestamp_(input_timestamp) {}

    /// Retrieve input packet at specific index.
    const Packet& Input(size_t index) const {
        if (index >= inputs_.size()) {
            throw std::out_of_range("Context input index out of range");
        }
        return inputs_[index];
    }

    /// Set output packet at specific index.
    void Output(size_t index, Packet packet) {
        if (outputs_.size() <= index) {
            outputs_.resize(index + 1);
        }
        outputs_[index] = std::move(packet);
    }

    /// Retrieve and consume output packet at specific index.
    Packet TakeOutput(size_t index) {
        if (index >= outputs_.size()) {
            return Packet::Empty();
        }
        Packet packet = std::move(outputs_[index]);
        outputs_[index] = Packet::Empty();
        return packet;
    }

    bool HasOutput(size_t index) const {
        return index < outputs_.size() && outputs_[index].has_value();
    }

    int64_t InputTimestamp() const {
        return input_timestamp_;
    }

private:
    std::vector<Packet> inputs_;
    std::vector<Packet> outputs_;
    int64_t input_timestamp_{0};
};

// ========== Method Tagged Value (Optimized) ==========

struct MethodTaggedValue {
    size_t method_id;
    Packet payload;
};

inline bool IsTaggedValue(const Packet& packet) {
    return packet.has_value() && packet.type() == TypeInfo::create<MethodTaggedValue>();
}

inline MethodTaggedValue AsTaggedValue(const Packet& packet) {
    return packet.cast<MethodTaggedValue>();
}

// ========== Heterogeneous Method Dispatch (New Architecture) ==========

// Forward declaration
class Node;

// Unified invoker signature: Type-erased wrapper
using MethodInvoker = std::function<Packet(Node*, const std::vector<Packet>&)>;

// Reflection metadata for a method
struct MethodMeta {
    MethodInvoker invoker;
    std::vector<TypeInfo> arg_types;
    TypeInfo return_type;
};

namespace detail {

// Helper: Cast Packet to specific argument type
template<typename ArgT>
decltype(auto) CastArg(const Packet& p, size_t index) {
    try {
        return p.cast<std::decay_t<ArgT>>();
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "Argument " + std::to_string(index) + " type mismatch: expected " + 
            TypeInfo::create<std::decay_t<ArgT>>().type_name + ", got " + p.type().type_name
        );
    }
}

// Helper: Invoke implementation
template <typename Derived, typename Ret, typename... Args, size_t... I>
Packet InvokeImpl(Node* node, Ret (Derived::*func)(Args...), 
                 const std::vector<Packet>& inputs, std::index_sequence<I...>) {
    auto* derived = static_cast<Derived*>(node);
    
    if constexpr (std::is_void_v<Ret>) {
        (derived->*func)(CastArg<Args>(inputs[I], I)...);
        return Packet::Empty();
    } else {
        // Note: The timestamp will be handled by the caller (BaseNode logic)
        // using the max input timestamp. Here we just return the payload with 0 ts.
        Ret result = (derived->*func)(CastArg<Args>(inputs[I], I)...);
        return Packet::From(std::move(result), 0);
    }
}

} // namespace detail

// Factory: Create a type-safe invoker from a member function pointer
template <typename Derived, typename Ret, typename... Args>
MethodInvoker CreateInvoker(Ret (Derived::*func)(Args...)) {
    return [func](Node* base_node, const std::vector<Packet>& inputs) -> Packet {
        if (inputs.size() != sizeof...(Args)) {
            throw std::runtime_error(
                "Argument count mismatch: expected " + std::to_string(sizeof...(Args)) + 
                ", got " + std::to_string(inputs.size())
            );
        }
        return detail::InvokeImpl<Derived, Ret, Args...>(
            base_node, func, inputs, std::make_index_sequence<sizeof...(Args)>{});
    };
}

// Factory: Get argument types
template <typename Derived, typename Ret, typename... Args>
std::vector<TypeInfo> GetArgTypes(Ret (Derived::*)(Args...)) {
    return { TypeInfo::create<std::decay_t<Args>>()... };
}

// Factory: Get return type
template <typename Derived, typename Ret, typename... Args>
TypeInfo GetReturnType(Ret (Derived::*)(Args...)) {
    return TypeInfo::create<Ret>();
}

// ========== Node Base Class ==========

class Node {
public:
    virtual ~Node() = default;

    // Build the task in the graph
    virtual void build(ExecutionGraph& g) = 0;

    virtual void connect() = 0;

    // Connection Logic
    struct UpstreamConnection {
        Node* node;
        size_t method_id;
        bool weak{false};
    };

    struct PortInfo {
        size_t index;
        size_t method_id;
        int arg_index;
        bool is_control;
    };

    struct MethodConfig {
        bool sync_enabled{false};
        size_t max_queue{0};
    };
    
    struct MuxConfig {
        Node* control_node;
        std::unordered_map<int, Node*> map;
    };

    std::vector<UpstreamConnection> upstreams_;
    std::vector<PortInfo> port_map_;
    std::vector<std::string> upstream_methods_debug_; 
    std::vector<std::deque<Packet>> port_buffers_;
    std::unordered_map<size_t, MethodConfig> method_configs_;
    std::vector<size_t> method_order_;
    bool method_order_customized_{false};
    std::unordered_map<size_t, std::unordered_map<int, MuxConfig>> mux_configs_;
    std::unordered_map<size_t, AnyCaster> port_converters_;

    void add_upstream(Node* n, std::string method = {}, int arg_idx = -1, bool weak = false) {
        size_t id = method.empty() ? ID_FORWARD : hash_string(method);
        size_t index = upstreams_.size();
        
        int actual_arg_idx = arg_idx;
        if (actual_arg_idx == -1) {
            actual_arg_idx = 0;
            for(const auto& p : port_map_) {
                if(p.method_id == id) {
                    actual_arg_idx = std::max(actual_arg_idx, p.arg_index + 1);
                }
            }
        }

        upstreams_.push_back({n, id, weak});
        port_map_.push_back({index, id, actual_arg_idx, id != ID_FORWARD});
        upstream_methods_debug_.push_back(std::move(method));
        port_buffers_.emplace_back();
        if (!method_order_customized_) {
            RegisterMethodOrder(id);
        }
    }
    
    void add_control_input(Node* n) {
        size_t index = upstreams_.size();
        upstreams_.push_back({n, 0});
        port_map_.push_back({index, 0, -1, true});
        upstream_methods_debug_.push_back("CONTROL");
        port_buffers_.emplace_back();
    }

    void ClearUpstreams() {
        upstreams_.clear();
        port_map_.clear();
        upstream_methods_debug_.clear();
        port_buffers_.clear();
        mux_configs_.clear();
        port_converters_.clear();
        if (!method_order_customized_) {
            method_order_.clear();
        }
    }

    // Lifecycle Management (Reflected)
    
    void Open(const std::vector<Packet>& args = {}) {
        if (opened_) return;
        
        // Try to invoke "Open" if registered.
        // We catch "Method not found" errors to make Open optional for users.
        try {
            invoke(ID_OPEN, args);
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("Method not found") == std::string::npos) {
                throw; // Rethrow if it's a different error (e.g. arg mismatch)
            }
        }
        opened_ = true;
    }

    void Close(const std::vector<Packet>& args = {}) {
        if (!opened_) return;

        try {
            invoke(ID_CLOSE, args);
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("Method not found") == std::string::npos) {
                throw;
            }
        }
        opened_ = false;
    }

    bool IsOpen() const { return opened_; }

    void ClearOutput() {
        output_packet_ = Packet::Empty();
    }

    virtual void Activate() {}
    
    void Stop() {
        if (graph_) {
            std::cout << "Node::Stop called! Stopping graph." << std::endl;
            graph_->keep_running = false;
        }
    }

    virtual void set_input(Node* upstream, int arg_idx = -1) {
        add_upstream(upstream, {}, arg_idx);
    }

    virtual void set_weak_input(Node* upstream, int arg_idx = -1) {
        add_upstream(upstream, {}, arg_idx, true);
    }
    
    virtual void set_input_for(const std::string& method, Node* upstream, int arg_idx = -1) {
        if (method.empty() || method == "forward") {
            set_input(upstream, arg_idx);
            return;
        }
        add_upstream(upstream, method, arg_idx);
    }
    
    void SetInputMux(const std::string& method, int arg_idx, Node* control, const std::unordered_map<int, Node*>& map) {
        size_t id = method.empty() || method == "forward" ? ID_FORWARD : hash_string(method);
        mux_configs_[id][arg_idx] = {control, map};
        bool control_found = false;
        for(const auto& u : upstreams_) { if(u.node == control) control_found = true; }
        if(!control_found) add_control_input(control);
    }

    void SetMethodOrder(const std::vector<std::string>& methods) {
        method_order_.clear();
        method_order_customized_ = true;
        for (const auto& name : methods) {
            size_t id = name.empty() || name == "forward" ? ID_FORWARD : hash_string(name);
            if (std::find(method_order_.begin(), method_order_.end(), id) == method_order_.end()) {
                method_order_.push_back(id);
            }
        }
        EnsureForwardLast();
    }

    void SetMethodSync(const std::string& method, bool enabled) {
        size_t id = method.empty() || method == "forward" ? ID_FORWARD : hash_string(method);
        method_configs_[id].sync_enabled = enabled;
    }

    void SetMethodQueueSize(const std::string& method, size_t max_queue) {
        size_t id = method.empty() || method == "forward" ? ID_FORWARD : hash_string(method);
        method_configs_[id].max_queue = max_queue;
    }

    virtual NodeTypeInfo get_type_info() const = 0;

    virtual std::string type_name() const {
        return "Node";
    }

    virtual std::vector<std::string> exposed_methods() const {
        return {"forward"};
    }

    virtual Packet invoke(size_t method_id, const std::vector<Packet>& inputs) {
        return Packet::Empty();
    }

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
    Packet output_packet_;

protected:
    tf::Task task_;
    ExecutionGraph* graph_{nullptr};
    
    // Helpers for dispatch logic
    void EnsurePortBufferSize() {
        if (port_buffers_.size() < upstreams_.size()) {
            port_buffers_.resize(upstreams_.size());
        }
    }

    void BufferPortInputs() {
        for (size_t i = 0; i < upstreams_.size(); ++i) {
            auto* node = upstreams_[i].node;
            if (!node) {
                continue;
            }
            Packet packet = node->output_packet_;
            if (!packet.has_value()) {
                continue;
            }
            port_buffers_[i].push_back(std::move(packet));
            const auto& config = GetMethodConfig(upstreams_[i].method_id);
            if (config.max_queue > 0) {
                while (port_buffers_[i].size() > config.max_queue) {
                    port_buffers_[i].pop_front();
                }
            }
        }
    }

    int FindPortIndex(Node* upstream, size_t method_id, int arg_idx) const {
        for(size_t i=0; i<port_map_.size(); ++i) {
            if (port_map_[i].method_id == method_id && 
                port_map_[i].arg_index == arg_idx && 
                upstreams_[i].node == upstream) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    
    int FindControlPortIndex(Node* control) const {
        for(size_t i=0; i<port_map_.size(); ++i) {
            if (port_map_[i].is_control && upstreams_[i].node == control) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    
    const std::vector<size_t>& EffectiveMethodOrder() const {
        if (!method_order_.empty()) {
            return method_order_;
        }
        static const std::vector<size_t> kDefault{ID_FORWARD};
        return kDefault;
    }
    
    const MethodConfig& GetMethodConfig(size_t method_id) const {
        auto it = method_configs_.find(method_id);
        if (it != method_configs_.end()) {
            return it->second;
        }
        static const MethodConfig kDefault;
        return kDefault;
    }

private:
    void RegisterMethodOrder(size_t method_id) {
        if (std::find(method_order_.begin(), method_order_.end(), method_id) != method_order_.end()) {
            return;
        }
        if (method_id == ID_FORWARD) {
            method_order_.push_back(method_id);
            return;
        }
        auto forward_it = std::find(method_order_.begin(), method_order_.end(), ID_FORWARD);
        if (forward_it == method_order_.end()) {
            method_order_.push_back(method_id);
        } else {
            method_order_.insert(forward_it, method_id);
        }
        EnsureForwardLast();
    }

    void EnsureForwardLast() {
        auto forward_it = std::remove(method_order_.begin(), method_order_.end(), ID_FORWARD);
        if (forward_it != method_order_.end()) {
            method_order_.erase(forward_it, method_order_.end());
        }
        if (!method_order_.empty()) {
            method_order_.push_back(ID_FORWARD);
        }
    }

    bool opened_{false};
    mutable std::vector<Node*> upstream_nodes_;
};

namespace detail {
template<typename Derived, typename MethodTable>
inline constexpr bool has_method_table_v = requires {
    { Derived::method_table() } -> std::same_as<MethodTable>;
};

template<typename T, typename = void>
struct has_context_forward : std::false_type {};

template<typename T>
struct has_context_forward<T, std::void_t<decltype(std::declval<T>().forward(static_cast<Context*>(nullptr)))>>
    : std::bool_constant<std::is_same_v<decltype(std::declval<T>().forward(static_cast<Context*>(nullptr))), void>> {};

template<typename T>
inline constexpr bool has_context_forward_v = has_context_forward<T>::value;
} // namespace detail

// Forward declaration
template<typename TupleT>
bool RegisterTupleType();

// ========== BaseNode ==========

template<typename Derived>
class BaseNode : public Node {
public:
    using Self = Derived;
    
    // Default constructor
    BaseNode() {
        // Note: Output tuple types should be registered manually by the user 
        // or via the EW_REGISTER_NODE mechanism if needed for Python unpacking.
    }

    void build(ExecutionGraph& g) override {
        graph_ = &g;
        
        task_ = g.taskflow.emplace([this]() {
            RunDispatch();
        });
        
        task_.name(TypeInfo::create<Derived>().type_name);
    }

    NodeTypeInfo get_type_info() const override {
        NodeTypeInfo info;
        const auto& registry = Derived::method_registry();
        for (const auto& [id, meta] : registry) {
             info.methods[id] = {meta.arg_types, meta.return_type};
        }
        return info;
    }

    std::string type_name() const override {
        return TypeInfo::create<Derived>().type_name;
    }

    void connect() override {
        // Build precedences
        for (const auto& conn : upstreams_) {
            if (conn.node && !conn.weak) {
                conn.node->get_task().precede(task_);
            }
        }
        PrepareInputConverters();
    }

    Packet invoke(size_t method_id, const std::vector<Packet>& inputs) override {
        const auto& registry = Derived::method_registry();
        auto it = registry.find(method_id);
        if (it == registry.end()) {
             throw std::runtime_error("Method not found in registry: " + std::to_string(method_id));
        }
        // Invoker handles argument count check and type casting/unpacking
        return it->second.invoker(this, inputs);
    }

protected:
    void PrepareInputConverters() {
        RegisterArithmeticConversions();
        port_converters_.clear();
        const auto& registry = Derived::method_registry();

        for (const auto& [method_id, meta] : registry) {
            for (size_t i = 0; i < meta.arg_types.size(); ++i) {
                const TypeInfo& to_type = meta.arg_types[i];
                
                // Check if Muxed
                std::vector<Node*> potential_sources;
                if (mux_configs_[method_id].count(i)) {
                    const auto& cfg = mux_configs_[method_id][i];
                    for(const auto& pair : cfg.map) potential_sources.push_back(pair.second);
                } else {
                    // Find standard upstream
                    for(const auto& u : upstreams_) {
                        int idx = FindPortIndex(u.node, method_id, i);
                        if(idx != -1) {
                            potential_sources.push_back(u.node);
                            break; 
                        }
                    }
                }
                
                for(Node* src : potential_sources) {
                    int port_index = FindPortIndex(src, method_id, i);
                    if (port_index == -1) continue; 
                    
                    TypeInfo from_type = UpstreamOutputType(src);
                    if (from_type == to_type) continue;
                    
                    if (!TypeConverterRegistry::instance().has_converter(*from_type.type_info, *to_type.type_info)) {
                        throw std::runtime_error(
                            "Type mismatch: cannot connect " + from_type.type_name +
                            " to " + to_type.type_name);
                    }
                    
                    port_converters_[port_index] = [from_type, to_type](const Packet& packet) {
                        if (!packet.has_value()) {
                            return Packet::Empty();
                        }
                        std::any converted = TypeConverterRegistry::instance().convert(
                            packet.data(), *from_type.type_info, *to_type.type_info);
                        if (!converted.has_value()) {
                            throw std::runtime_error(
                                "Failed to convert " + from_type.type_name + " to " + to_type.type_name);
                        }
                        return Packet::FromAny(std::move(converted), packet.timestamp);
                    };
                }
            }
        }
    }

    TypeInfo UpstreamOutputType(Node* node) const {
        if (!node) {
            return TypeInfo::create<void>();
        }
        NodeTypeInfo info = node->get_type_info();
        auto it = info.methods.find(ID_FORWARD);
        if (it == info.methods.end()) {
            return TypeInfo::create<void>();
        }
        return it->second.output_type;
    }

    void RunDispatch() {
        try {
            EnsurePortBufferSize();
            BufferPortInputs();

            const auto& order = EffectiveMethodOrder();
            const auto& registry = Derived::method_registry();
            bool output_produced = false;

            for (size_t method_id : order) {
                auto it = registry.find(method_id);
                if (it == registry.end()) continue;
                
                size_t required_args = it->second.arg_types.size();
                std::vector<Packet> inputs;
                inputs.reserve(required_args);
                
                bool method_ready = true;
                std::unordered_set<int> popped_controls;
                
                for(size_t i=0; i<required_args; ++i) {
                    // Check Mux
                    int selected_port = -1;
                    std::vector<int> discarded_ports;
                    
                    if (mux_configs_[method_id].count(i)) {
                        const auto& cfg = mux_configs_[method_id][i];
                        int control_port = FindControlPortIndex(cfg.control_node);
                        if (control_port != -1 && !port_buffers_[control_port].empty()) {
                            Packet control_pkt = port_buffers_[control_port].front(); 
                            
                            int choice = -1;
                            try {
                                if (control_pkt.type() == TypeInfo::create<bool>()) {
                                    choice = control_pkt.cast<bool>() ? 0 : 1;
                                } else if (control_pkt.type() == TypeInfo::create<int>()) {
                                    choice = control_pkt.cast<int>();
                                } else if (control_pkt.type() == TypeInfo::create<int64_t>()) {
                                    choice = static_cast<int>(control_pkt.cast<int64_t>());
                                } else {
                                    throw std::runtime_error("Mux control packet must be bool or int");
                                }
                            } catch (const std::exception&) {
                                throw;
                            }
                            
                            auto map_it = cfg.map.find(choice);
                            if (map_it != cfg.map.end()) {
                                selected_port = FindPortIndex(map_it->second, method_id, i);
                            } else {
                                throw std::runtime_error("Mux control value has no mapping");
                            }
                            
                            for(const auto& pair : cfg.map) {
                                if (pair.first != choice) {
                                    int p_idx = FindPortIndex(pair.second, method_id, i);
                                    if(p_idx != -1) discarded_ports.push_back(p_idx);
                                }
                            }
                        }
                    } else {
                        for(size_t p=0; p<port_map_.size(); ++p) {
                            if(port_map_[p].method_id == method_id && port_map_[p].arg_index == static_cast<int>(i)) {
                                selected_port = static_cast<int>(p);
                                break;
                            }
                        }
                    }
                    
                    for(int p_idx : discarded_ports) {
                        if(p_idx >= 0 && p_idx < port_buffers_.size() && !port_buffers_[p_idx].empty()) {
                            port_buffers_[p_idx].pop_front();
                        }
                    }
                    
                    if (selected_port != -1 && selected_port < port_buffers_.size() && !port_buffers_[selected_port].empty()) {
                        Packet pkt = port_buffers_[selected_port].front();
                        port_buffers_[selected_port].pop_front();
                        
                        if(port_converters_.count(selected_port)) {
                            inputs.push_back(port_converters_[selected_port](pkt));
                        } else {
                            inputs.push_back(std::move(pkt));
                        }
                    } else {
                        method_ready = false;
                        break;
                    }
                }
                
                if (!method_ready) continue;
                
                for(size_t i=0; i<required_args; ++i) {
                     if (mux_configs_[method_id].count(i)) {
                         int c_idx = FindControlPortIndex(mux_configs_[method_id][i].control_node);
                         if(c_idx != -1 && !popped_controls.count(c_idx) && !port_buffers_[c_idx].empty()) {
                             port_buffers_[c_idx].pop_front();
                             popped_controls.insert(c_idx);
                         }
                     }
                }
                
                // Invoke
                Packet result = it->second.invoker(this, inputs);

                if (result.has_value()) {
                    if (result.timestamp == 0) {
                        if (!inputs.empty()) {
                             result.timestamp = inputs[0].timestamp;
                        } else {
                             result.timestamp = Packet::NowNs();
                        }
                    }
                    output_packet_ = std::move(result);
                    output_produced = true;
                }
            }

            if (!output_produced) {
                output_packet_ = Packet::Empty();
            }

        } catch (const std::exception& e) {
            std::cerr << "Dispatch Error: " << e.what() << std::endl;
            output_packet_ = Packet::Empty();
        }
    }
};

// ========== SyncBarrier ==========
// (SyncBarrier remains mostly unchanged, but inherits Node directly)

template<typename... InputTs>
class SyncBarrier : public Node {
public:
    using OutputTuple = std::tuple<InputTs...>;

    explicit SyncBarrier(int64_t tolerance_ns = 0)
        : tolerance_ns_(tolerance_ns), buffers_(sizeof...(InputTs)) {}

    void build(ExecutionGraph& g) override {
        task_ = g.taskflow.emplace([this]() {
            try {
                BufferInputs();
                AlignAndEmit();
            } catch (const std::exception& e) {
                std::cerr << "Error in sync barrier: " << e.what() << std::endl;
                output_packet_ = Packet::Empty();
            }
        });
        task_.name("SyncBarrier");
    }

    void connect() override {
        for (const auto& conn : upstreams_) {
            if (conn.node) {
                conn.node->get_task().precede(task_);
            }
        }
    }

    NodeTypeInfo get_type_info() const override {
        // SyncBarrier behaves like a generic node with 1 method (implicit forward)
        NodeTypeInfo info;
        MethodInfo method;
        method.input_types = {TypeInfo::create<InputTs>()...};
        method.output_type = TypeInfo::create<OutputTuple>();
        info.methods[ID_FORWARD] = method;
        return info;
    }

private:
    int64_t tolerance_ns_;
    std::vector<std::deque<Packet>> buffers_;

    void BufferInputs() {
        for (size_t i = 0; i < buffers_.size() && i < upstreams_.size(); ++i) {
            if (!upstreams_[i].node) {
                continue;
            }
            Packet packet = upstreams_[i].node->output_packet_;
            if (!packet.has_value()) {
                continue;
            }
            buffers_[i].push_back(std::move(packet));
        }
    }

    bool BuffersReady() const {
        if (buffers_.empty()) {
            return false;
        }
        return std::all_of(buffers_.begin(), buffers_.end(), [](const auto& buffer) {
            return !buffer.empty();
        });
    }

    int64_t MinTimestamp() const {
        int64_t min_ts = std::numeric_limits<int64_t>::max();
        for (const auto& buffer : buffers_) {
            min_ts = std::min(min_ts, buffer.front().timestamp);
        }
        return min_ts == std::numeric_limits<int64_t>::max() ? 0 : min_ts;
    }

    int64_t MaxTimestamp() const {
        int64_t max_ts = 0;
        for (const auto& buffer : buffers_) {
            max_ts = std::max(max_ts, buffer.front().timestamp);
        }
        return max_ts;
    }

    void DropEarlier(int64_t min_ts) {
        for (auto& buffer : buffers_) {
            if (!buffer.empty() && buffer.front().timestamp == min_ts) {
                buffer.pop_front();
            }
        }
    }

    template<size_t... Index>
    OutputTuple BuildOutput(std::index_sequence<Index...>) {
        return std::make_tuple(buffers_[Index].front().template cast<InputTs>()...);
    }

    void PopAligned() {
        for (auto& buffer : buffers_) {
            if (!buffer.empty()) {
                buffer.pop_front();
            }
        }
    }

    void AlignAndEmit() {
        while (BuffersReady()) {
            int64_t min_ts = MinTimestamp();
            int64_t max_ts = MaxTimestamp();
            if (max_ts - min_ts <= tolerance_ns_) {
                OutputTuple output = BuildOutput(std::index_sequence_for<InputTs...>{});
                PopAligned();
                output_packet_ = Packet::From(std::move(output), max_ts);
                return;
            }
            DropEarlier(min_ts);
        }
        output_packet_ = Packet::Empty();
    }
};

// ========== TupleGetNode ==========

template<size_t Index, typename TupleT>
class TupleGetNode : public BaseNode<TupleGetNode<Index, TupleT>> {
public:
    using Self = TupleGetNode<Index, TupleT>;
    using ElementType = std::tuple_element_t<Index, TupleT>;

    ElementType forward(TupleT input) {
        return std::get<Index>(input);
    }
    
    EW_ENABLE_METHODS(forward)
};

namespace detail {

struct TupleRegistryEntry {
    size_t size;
    std::function<std::shared_ptr<Node>(size_t)> factory;
};

inline std::unordered_map<std::type_index, TupleRegistryEntry>& TupleRegistry() {
    static std::unordered_map<std::type_index, TupleRegistryEntry> registry;
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
    const auto type_key = type_info.type_index;
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
    auto it = registry.find(tuple_type.type_index);
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
    auto it = registry.find(tuple_type.type_index);
    if (it == registry.end()) {
        return 0;
    }
    return it->second.size;
}

// ========== Executor ==========

class Executor {
public:
    void open(const std::vector<Node*>& nodes) {
        for (auto* node : nodes) {
            if (node) {
                node->Open();
            }
        }
    }

    void close(const std::vector<Node*>& nodes) {
        for (auto* node : nodes) {
            if (node) {
                node->Close();
            }
        }
    }

    void run(ExecutionGraph& g) {
        g.keep_running = true;
        
        while (g.keep_running) {
             g.executor.run(g.taskflow).wait();
        }
    }
};

} // namespace easywork
