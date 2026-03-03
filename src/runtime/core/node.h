#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "runtime/core/execution_graph.h"
#include "runtime/types/type_converter.h"

namespace easywork {

class Node {
public:
    virtual ~Node() = default;

    virtual void build(ExecutionGraph& g) = 0;
    virtual void connect() = 0;

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
        size_t max_queue{0};
    };

    struct MuxConfig {
        Node* control_node;
        std::unordered_map<int, Node*> map;
    };

    void add_upstream(Node* n, std::string method = {}, int arg_idx = -1, bool weak = false) {
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify connections.");
        }
        size_t id = method.empty() ? ID_FORWARD : hash_string(method);
        size_t index = upstreams_.size();

        int actual_arg_idx = arg_idx;
        if (actual_arg_idx == -1) {
            actual_arg_idx = 0;
            for (const auto& p : port_map_) {
                if (p.method_id == id) {
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

    void Open(const std::vector<Packet>& args = {}) {
        if (opened_) {
            return;
        }

        if (HasMethod(ID_OPEN)) {
            invoke(ID_OPEN, args);
        }
        opened_ = true;
    }

    void Close(const std::vector<Packet>& args = {}) {
        if (!opened_) {
            return;
        }

        if (HasMethod(ID_CLOSE)) {
            invoke(ID_CLOSE, args);
        }
        opened_ = false;
    }

    bool IsOpen() const { return opened_; }

    virtual void ClearOutput() {
        output_packet_ = Packet::Empty();
    }

    virtual void ClearBuffers() {
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

    void SetInputMux(const std::string& method,
                     int arg_idx,
                     Node* control,
                     const std::unordered_map<int, Node*>& map) {
        if (graph_ && graph_->IsLocked()) {
            throw std::runtime_error("Graph is locked. Cannot modify connections.");
        }
        size_t id = method.empty() || method == "forward" ? ID_FORWARD : hash_string(method);
        mux_configs_[id][arg_idx] = {control, map};
        bool control_found = false;
        for (const auto& u : upstreams_) {
            if (u.node == control) {
                control_found = true;
                break;
            }
        }
        if (!control_found) {
            add_control_input(control);
        }
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

    virtual bool HasMethod(size_t method_id) const {
        return method_id == ID_FORWARD;
    }

    virtual Packet invoke(size_t method_id, const std::vector<Packet>& inputs) {
        (void)method_id;
        (void)inputs;
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

    const std::vector<UpstreamConnection>& connections() const {
        return upstreams_;
    }

protected:
    struct DispatchMuxPlan {
        bool enabled{false};
        int control_port{-1};
        std::unordered_map<int, int> choice_port;
        std::vector<int> all_ports;
    };

    struct DispatchMethodPlan {
        size_t method_id{0};
        size_t required_args{0};
        std::vector<int> port_indices;
        std::vector<DispatchMuxPlan> mux_plans;
    };

    tf::Task task_;
    ExecutionGraph* graph_{nullptr};

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
        for (size_t i = 0; i < port_map_.size(); ++i) {
            if (port_map_[i].method_id == method_id && port_map_[i].arg_index == arg_idx &&
                upstreams_[i].node == upstream) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int FindControlPortIndex(Node* control) const {
        for (size_t i = 0; i < port_map_.size(); ++i) {
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

    const std::vector<UpstreamConnection>& UpstreamConnections() const {
        return upstreams_;
    }

    const std::vector<PortInfo>& PortMappings() const {
        return port_map_;
    }

    std::vector<std::deque<Packet>>& MutablePortBuffers() {
        return port_buffers_;
    }

    const std::vector<std::deque<Packet>>& PortBuffers() const {
        return port_buffers_;
    }

    const std::unordered_map<size_t, std::unordered_map<int, MuxConfig>>& MuxConfigs() const {
        return mux_configs_;
    }

    std::unordered_map<size_t, AnyCaster>& MutablePortConverters() {
        return port_converters_;
    }

    const std::unordered_map<size_t, AnyCaster>& PortConverters() const {
        return port_converters_;
    }

    const Packet& OutputPacket() const {
        return output_packet_;
    }

    void SetOutputPacket(Packet packet) {
        output_packet_ = std::move(packet);
    }

    const MethodConfig& GetMethodConfig(size_t method_id) const {
        auto it = method_configs_.find(method_id);
        if (it != method_configs_.end()) {
            return it->second;
        }
        static const MethodConfig kDefault;
        return kDefault;
    }

    static int DecodeMuxChoiceDefault(const Packet& control_pkt) {
        if (control_pkt.type() == TypeInfo::create<bool>()) {
            return control_pkt.cast<bool>() ? 0 : 1;
        }
        if (control_pkt.type() == TypeInfo::create<int>()) {
            return control_pkt.cast<int>();
        }
        if (control_pkt.type() == TypeInfo::create<int64_t>()) {
            return static_cast<int>(control_pkt.cast<int64_t>());
        }
        throw std::runtime_error("Mux control packet must be bool or int");
    }

    void BuildDispatchPlans(const std::vector<std::pair<size_t, size_t>>& method_specs,
                            std::vector<DispatchMethodPlan>* plans) const {
        if (!plans) {
            throw std::runtime_error("Dispatch plans output cannot be null");
        }
        plans->clear();
        plans->reserve(method_specs.size());

        for (const auto& [method_id, required_args] : method_specs) {
            DispatchMethodPlan plan;
            plan.method_id = method_id;
            plan.required_args = required_args;
            plan.port_indices.assign(required_args, -1);
            plan.mux_plans.resize(required_args);

            for (size_t i = 0; i < required_args; ++i) {
                auto mux_it = mux_configs_.find(method_id);
                if (mux_it != mux_configs_.end() && mux_it->second.count(static_cast<int>(i))) {
                    const auto& cfg = mux_it->second.at(static_cast<int>(i));
                    DispatchMuxPlan mux_plan;
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
            plans->push_back(std::move(plan));
        }
    }

    template <typename DecodeChoiceFn, typename ConvertFn, typename InvokeFn>
    void RunDispatchPlans(const std::vector<DispatchMethodPlan>& plans,
                          DecodeChoiceFn&& decode_choice,
                          ConvertFn&& convert_packet,
                          InvokeFn&& invoke_ready_method) {
        if (graph_ && graph_->skip_current.load()) {
            output_packet_ = Packet::Empty();
            return;
        }

        EnsurePortBufferSize();
        BufferPortInputs();

        if (dispatch_control_marks_.size() != port_buffers_.size()) {
            dispatch_control_marks_.assign(port_buffers_.size(), 0);
        }

        bool output_produced = false;

        for (size_t plan_index = 0; plan_index < plans.size(); ++plan_index) {
            const auto& plan = plans[plan_index];
            std::vector<Packet> inputs;
            inputs.reserve(plan.required_args);

            if (!dispatch_control_marks_.empty()) {
                std::fill(dispatch_control_marks_.begin(), dispatch_control_marks_.end(), 0);
            }

            bool method_ready = true;
            for (size_t i = 0; i < plan.required_args; ++i) {
                int selected_port = -1;

                if (plan.mux_plans.size() > i && plan.mux_plans[i].enabled) {
                    const auto& mux_plan = plan.mux_plans[i];
                    if (mux_plan.control_port == -1 ||
                        mux_plan.control_port >= static_cast<int>(port_buffers_.size()) ||
                        port_buffers_[mux_plan.control_port].empty()) {
                        method_ready = false;
                        break;
                    }

                    const Packet& control_pkt = port_buffers_[mux_plan.control_port].front();
                    int choice = decode_choice(control_pkt);

                    auto map_it = mux_plan.choice_port.find(choice);
                    if (map_it == mux_plan.choice_port.end()) {
                        throw std::runtime_error("Mux control value has no mapping");
                    }
                    selected_port = map_it->second;

                    for (int p_idx : mux_plan.all_ports) {
                        if (p_idx != selected_port && p_idx >= 0 &&
                            p_idx < static_cast<int>(port_buffers_.size()) &&
                            !port_buffers_[p_idx].empty()) {
                            port_buffers_[p_idx].pop_front();
                        }
                    }
                } else if (plan.port_indices.size() > i) {
                    selected_port = plan.port_indices[i];
                }

                if (selected_port == -1 || selected_port >= static_cast<int>(port_buffers_.size()) ||
                    port_buffers_[selected_port].empty()) {
                    method_ready = false;
                    break;
                }

                Packet pkt = port_buffers_[selected_port].front();
                port_buffers_[selected_port].pop_front();
                inputs.push_back(convert_packet(selected_port, pkt));
            }

            if (!method_ready) {
                continue;
            }

            for (size_t i = 0; i < plan.required_args; ++i) {
                if (!(plan.mux_plans.size() > i && plan.mux_plans[i].enabled)) {
                    continue;
                }
                int control_idx = plan.mux_plans[i].control_port;
                if (control_idx == -1 || control_idx >= static_cast<int>(dispatch_control_marks_.size())) {
                    continue;
                }
                if (!dispatch_control_marks_[control_idx] && !port_buffers_[control_idx].empty()) {
                    port_buffers_[control_idx].pop_front();
                    dispatch_control_marks_[control_idx] = 1;
                }
            }

            Packet result = invoke_ready_method(plan_index, plan, inputs);
            if (!result.has_value()) {
                continue;
            }
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

        if (!output_produced) {
            output_packet_ = Packet::Empty();
        }
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

    std::vector<UpstreamConnection> upstreams_;
    std::vector<PortInfo> port_map_;
    std::vector<std::deque<Packet>> port_buffers_;
    std::unordered_map<size_t, MethodConfig> method_configs_;
    std::vector<size_t> method_order_;
    bool method_order_customized_{false};
    std::unordered_map<size_t, std::unordered_map<int, MuxConfig>> mux_configs_;
    std::unordered_map<size_t, AnyCaster> port_converters_;
    Packet output_packet_;

    bool opened_{false};
    mutable std::vector<Node*> upstream_nodes_;
    std::vector<uint8_t> dispatch_control_marks_;
};

} // namespace easywork
