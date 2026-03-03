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
#include <cstdint>
#include "runtime/types/type_system.h"
#include "runtime/types/type_converter.h"
#include "runtime/core/error_codes.h"
#include "runtime/core/logger.h"
#include "runtime/registry/macros.h"

namespace easywork {

// ========== Constants ==========

constexpr size_t ID_FORWARD = hash_string("forward");
constexpr size_t ID_OPEN = hash_string("Open");
constexpr size_t ID_CLOSE = hash_string("Close");

// ========== Error Policy ==========

enum class ErrorPolicy {
    FailFast = 0,
    SkipCurrentData = 1,
};

// Forward declaration
class Node;

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
    std::atomic<bool> skip_current{false};
    std::atomic<bool> locked{false};
    ErrorPolicy error_policy{ErrorPolicy::FailFast};

    /// Clears the graph and resets the running state.
    void Reset() {
        taskflow.clear();
        keep_running = true;
        skip_current = false;
        locked = false;
        ClearErrors();
        nodes_.clear();
    }

    void SetErrorPolicy(ErrorPolicy policy) {
        error_policy = policy;
    }

    ErrorPolicy GetErrorPolicy() const {
        return error_policy;
    }

    void ReportError(ErrorCode code,
                     const std::string& message,
                     std::unordered_map<std::string, std::string> fields = {}) {
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = message;
            ++error_count_;
            last_error_code_.store(static_cast<std::uint32_t>(code));
        }
        if (!fields.contains("error_code")) {
            fields.emplace("error_code", std::string(ErrorCodeName(code)));
        }
        fields["detail"] = message;
        LogRuntime(RuntimeLogLevel::Error, "Runtime error", std::move(fields));
        if (error_policy == ErrorPolicy::FailFast) {
            keep_running = false;
        } else {
            skip_current = true;
        }
    }

    void ClearErrors() {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_.clear();
        error_count_ = 0;
        last_error_code_.store(static_cast<std::uint32_t>(ErrorCode::Ok));
    }

    std::string LastError() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

    size_t ErrorCount() const {
        return error_count_.load();
    }

    ErrorCode LastErrorCode() const {
        return static_cast<ErrorCode>(last_error_code_.load());
    }

    std::string LastErrorCodeName() const {
        return std::string(ErrorCodeName(LastErrorCode()));
    }

    void RegisterNode(Node* node);
    void ClearOutputsAndBuffers();

    void Lock() { locked = true; }
    void Unlock() { locked = false; }
    bool IsLocked() const { return locked.load(); }

private:
    mutable std::mutex error_mutex_;
    std::string last_error_;
    std::atomic<size_t> error_count_{0};
    std::atomic<std::uint32_t> last_error_code_{static_cast<std::uint32_t>(ErrorCode::Ok)};
    std::vector<Node*> nodes_;
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

// ========== Heterogeneous Method Dispatch (New Architecture) ==========

// Forward declaration
class Node;

// Unified invoker signature: Type-erased wrapper
using MethodInvoker = std::function<Packet(Node*, const std::vector<Packet>&)>;
using FastInvoker = Packet (*)(Node*, const std::vector<Packet>&);

// Reflection metadata for a method
struct MethodMeta {
    MethodInvoker invoker;
    FastInvoker fast_invoker{nullptr};
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

namespace detail {

template <typename T>
inline constexpr bool kFastPathType = std::is_trivially_copyable_v<std::decay_t<T>>;

template <typename Ret, typename... Args>
inline constexpr bool kFastPathCompatible =
    (std::is_void_v<Ret> || kFastPathType<Ret>) && (kFastPathType<Args> && ...);

template <typename T>
struct MethodTraits;

template <typename Class, typename Ret, typename... Args>
struct MethodTraits<Ret (Class::*)(Args...)> {
    using ClassType = Class;
    using ReturnType = Ret;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t Arity = sizeof...(Args);
};

template <auto Func, typename Traits, size_t... I>
Packet FastInvokeImpl(Node* base_node, const std::vector<Packet>& inputs, std::index_sequence<I...>) {
    using Derived = typename Traits::ClassType;
    using Ret = typename Traits::ReturnType;
    if (inputs.size() != sizeof...(I)) {
        throw std::runtime_error(
            "Argument count mismatch: expected " + std::to_string(sizeof...(I)) +
            ", got " + std::to_string(inputs.size())
        );
    }
    return InvokeImpl<Derived, Ret, std::tuple_element_t<I, typename Traits::ArgsTuple>...>(
        base_node, Func, inputs, std::index_sequence<I...>{});
}

template <auto Func>
Packet FastInvoke(Node* base_node, const std::vector<Packet>& inputs) {
    using Traits = MethodTraits<decltype(Func)>;
    return FastInvokeImpl<Func, Traits>(base_node, inputs, std::make_index_sequence<Traits::Arity>{});
}

template <typename Traits, size_t... I>
constexpr bool FastPathCompatibleTuple(std::index_sequence<I...>) {
    return (std::is_void_v<typename Traits::ReturnType> || kFastPathType<typename Traits::ReturnType>) &&
           (kFastPathType<std::tuple_element_t<I, typename Traits::ArgsTuple>> && ...);
}

} // namespace detail

template <auto Func>
FastInvoker CreateFastInvoker() {
    using Traits = detail::MethodTraits<decltype(Func)>;
    if constexpr (detail::FastPathCompatibleTuple<Traits>(std::make_index_sequence<Traits::Arity>{})) {
        return &detail::FastInvoke<Func>;
    } else {
        return nullptr;
    }
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
    std::vector<std::deque<Packet>> port_buffers_;
    std::unordered_map<size_t, MethodConfig> method_configs_;
    std::vector<size_t> method_order_;
    bool method_order_customized_{false};
    std::unordered_map<size_t, std::unordered_map<int, MuxConfig>> mux_configs_;
    std::unordered_map<size_t, AnyCaster> port_converters_;

    void add_upstream(Node* n, std::string method = {}, int arg_idx = -1, bool weak = false) {
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify connections.");
        }
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
        port_buffers_.emplace_back();
        if (!method_order_customized_) {
            RegisterMethodOrder(id);
        }
    }
    
    void add_control_input(Node* n) {
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify connections.");
        }
        size_t index = upstreams_.size();
        upstreams_.push_back({n, 0});
        port_map_.push_back({index, 0, -1, true});
        port_buffers_.emplace_back();
    }

    void ClearUpstreams() {
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify connections.");
        }
        upstreams_.clear();
        port_map_.clear();
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

    void ClearBuffers() {
        for (auto& buffer : port_buffers_) {
            buffer.clear();
        }
    }

    virtual void Activate() {}
    
    void Stop() {
        if (graph_) {
            LogRuntime(RuntimeLogLevel::Info, "Node requested graph stop", {
                {"node", type_name()},
                {"event", "node_stop"},
            });
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
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify connections.");
        }
        size_t id = method.empty() || method == "forward" ? ID_FORWARD : hash_string(method);
        mux_configs_[id][arg_idx] = {control, map};
        bool control_found = false;
        for(const auto& u : upstreams_) { if(u.node == control) control_found = true; }
        if(!control_found) add_control_input(control);
    }

    void SetMethodOrder(const std::vector<std::string>& methods) {
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify method order.");
        }
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
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify method sync.");
        }
        size_t id = method.empty() || method == "forward" ? ID_FORWARD : hash_string(method);
        method_configs_[id].sync_enabled = enabled;
    }

    void SetMethodQueueSize(const std::string& method, size_t max_queue) {
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify method queue size.");
        }
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
        g.RegisterNode(this);
        
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
        BuildDispatchPlan();
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
    struct MuxPlan {
        bool enabled{false};
        int control_port{-1};
        std::unordered_map<int, int> choice_port;
        std::vector<int> all_ports;
    };

    struct MethodPlan {
        size_t method_id{0};
        const MethodMeta* meta{nullptr};
        const MethodInvoker* invoker{nullptr};
        FastInvoker fast_invoker{nullptr};
        std::vector<int> port_indices;
        std::vector<MuxPlan> mux_plans;
    };

    void PrepareInputConverters() {
        RegisterArithmeticConversions();
        port_converters_.clear();
        port_converters_fast_.clear();
        port_has_converter_.clear();
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

        const size_t port_count = upstreams_.size();
        port_converters_fast_.resize(port_count);
        port_has_converter_.assign(port_count, 0);
        for (const auto& [port_idx, converter] : port_converters_) {
            if (port_idx < port_count) {
                port_converters_fast_[port_idx] = converter;
                port_has_converter_[port_idx] = 1;
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

    void BuildDispatchPlan() {
        method_plans_.clear();

        const auto& order = EffectiveMethodOrder();
        const auto& registry = Derived::method_registry();
        for (size_t method_id : order) {
            auto it = registry.find(method_id);
            if (it == registry.end()) {
                continue;
            }

            MethodPlan plan;
            plan.method_id = method_id;
            plan.meta = &it->second;
            plan.invoker = &it->second.invoker;
            plan.fast_invoker = it->second.fast_invoker;

            const size_t required_args = it->second.arg_types.size();
            plan.port_indices.assign(required_args, -1);
            plan.mux_plans.resize(required_args);

            for (size_t i = 0; i < required_args; ++i) {
                auto mux_it = mux_configs_.find(method_id);
                if (mux_it != mux_configs_.end() && mux_it->second.count(i)) {
                    const auto& cfg = mux_it->second.at(i);
                    MuxPlan mux_plan;
                    mux_plan.enabled = true;
                    mux_plan.control_port = FindControlPortIndex(cfg.control_node);
                    if (mux_plan.control_port == -1) {
                        throw std::runtime_error("Mux control port not found during plan build");
                    }
                    for (const auto& pair : cfg.map) {
                        int port_index = FindPortIndex(pair.second, method_id, static_cast<int>(i));
                        if (port_index == -1) {
                            throw std::runtime_error("Mux input port not found during plan build");
                        }
                        mux_plan.choice_port.emplace(pair.first, port_index);
                        mux_plan.all_ports.push_back(port_index);
                    }
                    plan.mux_plans[i] = std::move(mux_plan);
                } else {
                    for (size_t p = 0; p < port_map_.size(); ++p) {
                        if (port_map_[p].method_id == method_id &&
                            port_map_[p].arg_index == static_cast<int>(i)) {
                            plan.port_indices[i] = static_cast<int>(p);
                            break;
                        }
                    }
                }
            }

            method_plans_.push_back(std::move(plan));
        }
        plan_built_ = true;
    }

    void RunDispatch() {
        try {
            if (graph_ && graph_->skip_current.load()) {
                output_packet_ = Packet::Empty();
                return;
            }

            if (!plan_built_) {
                BuildDispatchPlan();
            }

            EnsurePortBufferSize();
            BufferPortInputs();

            bool output_produced = false;

            for (const auto& plan : method_plans_) {
                if (!plan.meta) {
                    continue;
                }

                size_t required_args = plan.meta->arg_types.size();
                std::vector<Packet> inputs;
                inputs.reserve(required_args);
                
                bool method_ready = true;
                if (!popped_controls_.empty()) {
                    std::fill(popped_controls_.begin(), popped_controls_.end(), 0);
                }
                
                for(size_t i=0; i<required_args; ++i) {
                    // Check Mux
                    int selected_port = -1;
                    std::vector<int> discarded_ports;

                    if (plan.mux_plans.size() > i && plan.mux_plans[i].enabled) {
                        const auto& mux_plan = plan.mux_plans[i];
                        int control_port = mux_plan.control_port;
                        if (control_port != -1 && !port_buffers_[control_port].empty()) {
                            Packet control_pkt = port_buffers_[control_port].front();

                            int choice = -1;
                            if (control_pkt.type() == TypeInfo::create<bool>()) {
                                choice = control_pkt.cast<bool>() ? 0 : 1;
                            } else if (control_pkt.type() == TypeInfo::create<int>()) {
                                choice = control_pkt.cast<int>();
                            } else if (control_pkt.type() == TypeInfo::create<int64_t>()) {
                                choice = static_cast<int>(control_pkt.cast<int64_t>());
                            } else {
                                throw std::runtime_error("Mux control packet must be bool or int");
                            }

                            auto map_it = mux_plan.choice_port.find(choice);
                            if (map_it != mux_plan.choice_port.end()) {
                                selected_port = map_it->second;
                            } else {
                                throw std::runtime_error("Mux control value has no mapping");
                            }

                            discarded_ports.reserve(mux_plan.all_ports.size());
                            for (int p_idx : mux_plan.all_ports) {
                                if (p_idx != selected_port) {
                                    discarded_ports.push_back(p_idx);
                                }
                            }
                        }
                    } else {
                        if (plan.port_indices.size() > i) {
                            selected_port = plan.port_indices[i];
                        }
                    }
                    
                    for(int p_idx : discarded_ports) {
                        if(p_idx >= 0 && p_idx < port_buffers_.size() && !port_buffers_[p_idx].empty()) {
                            port_buffers_[p_idx].pop_front();
                        }
                    }
                    
                    if (selected_port != -1 && selected_port < static_cast<int>(port_buffers_.size()) &&
                        !port_buffers_[selected_port].empty()) {
                        Packet pkt = port_buffers_[selected_port].front();
                        port_buffers_[selected_port].pop_front();
                        
                        if (selected_port >= 0 && static_cast<size_t>(selected_port) < port_has_converter_.size() &&
                            port_has_converter_[selected_port]) {
                            inputs.push_back(port_converters_fast_[selected_port](pkt));
                        } else {
                            inputs.push_back(std::move(pkt));
                        }
                    } else {
                        method_ready = false;
                        break;
                    }
                }
                
                if (!method_ready) continue;
                
                if (popped_controls_.empty()) {
                    popped_controls_.assign(port_buffers_.size(), 0);
                }
                for (size_t i = 0; i < required_args; ++i) {
                    if (plan.mux_plans.size() > i && plan.mux_plans[i].enabled) {
                        int c_idx = plan.mux_plans[i].control_port;
                        if (c_idx != -1 && c_idx < static_cast<int>(popped_controls_.size()) &&
                            !popped_controls_[c_idx] && !port_buffers_[c_idx].empty()) {
                            port_buffers_[c_idx].pop_front();
                            popped_controls_[c_idx] = 1;
                        }
                    }
                }
                
                // Invoke
                Packet result = Packet::Empty();
                if (plan.fast_invoker) {
                    result = plan.fast_invoker(this, inputs);
                } else if (plan.invoker) {
                    result = (*plan.invoker)(this, inputs);
                }

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
            if (graph_) {
                graph_->ReportError(ErrorCode::DispatchError, std::string("Dispatch Error: ") + e.what(), {
                    {"node", type_name()},
                    {"event", "dispatch_exception"},
                });
            } else {
                std::cerr << "Dispatch Error: " << e.what() << std::endl;
            }
            output_packet_ = Packet::Empty();
        }
    }

    std::vector<MethodPlan> method_plans_;
    bool plan_built_{false};
    std::vector<AnyCaster> port_converters_fast_;
    std::vector<uint8_t> port_has_converter_;
    std::vector<uint8_t> popped_controls_;
};

// ========== SyncBarrier ==========
// (SyncBarrier removed as it was unused and potentially unsafe)

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
        g.Lock();
        const auto start = std::chrono::steady_clock::now();
        size_t iterations = 0;
        LogRuntime(RuntimeLogLevel::Info, "Executor started", {
            {"event", "executor_start"},
        });
        while (g.keep_running) {
            g.skip_current = false;
            ++iterations;
            g.executor.run(g.taskflow).wait();
            if (g.skip_current && g.error_policy == ErrorPolicy::SkipCurrentData) {
                g.ClearOutputsAndBuffers();
                LogRuntime(RuntimeLogLevel::Warn, "Skipped current data after error", {
                    {"event", "skip_current_data"},
                    {"error_code", "EW_SKIP_CURRENT_DATA"},
                });
            }
        }
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
        std::string stop_reason = "node_stop_or_completion";
        if (!g.LastError().empty()) {
            stop_reason = g.GetErrorPolicy() == ErrorPolicy::FailFast
                              ? "failfast_error"
                              : "error_skip_current";
        }
        LogRuntime(RuntimeLogLevel::Info, "Executor stopped", {
            {"event", "executor_summary"},
            {"iterations", std::to_string(iterations)},
            {"elapsed_ms", std::to_string(elapsed_ms)},
            {"error_count", std::to_string(g.ErrorCount())},
            {"last_error_code", g.LastErrorCodeName()},
            {"stop_reason", stop_reason},
        });
        g.Unlock();
    }
};

inline void ExecutionGraph::RegisterNode(Node* node) {
    if (!node) {
        return;
    }
    if (std::find(nodes_.begin(), nodes_.end(), node) == nodes_.end()) {
        nodes_.push_back(node);
    }
}

inline void ExecutionGraph::ClearOutputsAndBuffers() {
    for (auto* node : nodes_) {
        if (!node) {
            continue;
        }
        node->ClearOutput();
        node->ClearBuffers();
    }
}

} // namespace easywork
