#pragma once

#include "runtime/core/core.h"
#include "runtime/registry/node_registry.h"
#include <algorithm>
#include <unordered_set>

namespace easywork {

class IfNode : public Node {
public:
    void build(ExecutionGraph& g) override {
        graph_ = &g;
        g.RegisterNode(this);
        true_task_ = g.taskflow.emplace([]() {});
        false_task_ = g.taskflow.emplace([]() {});
        
        task_ = g.taskflow.emplace([this]() { EvaluateCondition(); });
        switch_task_ = g.taskflow.emplace([this]() { return SelectBranch(); });
        
        task_.name("IfNode");
        switch_task_.name("IfNode.Switch");
        true_task_.name("IfNode.true");
        false_task_.name("IfNode.false");
        
        task_.precede(switch_task_);
        switch_task_.precede(true_task_, false_task_);
    }

    void connect() override {
        for (const auto& conn : upstreams_) {
            if (conn.node) {
                conn.node->get_task().precede(task_);
            }
        }

        for (Node* node : true_nodes_) {
            if (node) {
                true_task_.precede(node->get_task());
            }
        }
        for (Node* node : false_nodes_) {
            if (node) {
                false_task_.precede(node->get_task());
            }
        }
    }

    NodeTypeInfo get_type_info() const override {
        NodeTypeInfo info;
        MethodInfo method;
        method.input_types = {TypeInfo::create<bool>()};
        method.output_type = TypeInfo::create<bool>();
        info.methods[ID_FORWARD] = method;
        return info;
    }

    std::string type_name() const override {
        return "IfNode";
    }

    std::vector<std::string> exposed_methods() const override {
        return {"forward"};
    }

    void AddTrueBranch(Node* node) {
        AddBranchNode(node, true_nodes_);
    }

    void AddFalseBranch(Node* node) {
        AddBranchNode(node, false_nodes_);
    }

private:
    void EvaluateCondition() {
        try {
            if (graph_ && graph_->skip_current.load()) {
                output_packet_ = Packet::Empty();
                return;
            }

            EnsurePortBufferSize();
            BufferPortInputs();
            
            int port_index = -1;
            for (size_t i = 0; i < port_map_.size(); ++i) {
                if (port_map_[i].method_id == ID_FORWARD) {
                    port_index = static_cast<int>(i);
                    break;
                }
            }

            if (port_index == -1 || port_buffers_[port_index].empty()) {
                output_packet_ = Packet::Empty();
                return;
            }

            size_t p_idx = static_cast<size_t>(port_index);
            Packet packet = port_buffers_[p_idx].front();
            port_buffers_[p_idx].pop_front();

            bool cond = false;
            if (packet.has_value()) {
                const auto packet_type = packet.type();
                if (packet_type == TypeInfo::create<bool>()) {
                    cond = packet.cast<bool>();
                } else if (packet_type == TypeInfo::create<int>()) {
                    cond = packet.cast<int>() != 0;
                } else if (packet_type == TypeInfo::create<int64_t>()) {
                    cond = packet.cast<int64_t>() != 0;
                } else {
                    throw std::runtime_error("IfNode condition must be bool or int");
                }
            }

            output_packet_ = Packet::From(cond, packet.timestamp);
        } catch (const std::exception& e) {
            if (graph_) {
                graph_->ReportError(std::string("IfNode error: ") + e.what());
            } else {
                std::cerr << "IfNode error: " << e.what() << std::endl;
            }
            output_packet_ = Packet::Empty();
        }
    }
    
    int SelectBranch() {
        if (!output_packet_.has_value()) return 1;
        try {
            return output_packet_.cast<bool>() ? 0 : 1;
        } catch(...) { return 1; }
    }

    void AddBranchNode(Node* node, std::vector<Node*>& target) {
        if (!node) {
            return;
        }
        if (std::find(target.begin(), target.end(), node) != target.end()) {
            return;
        }
        target.push_back(node);
    }

    std::vector<Node*> true_nodes_;
    std::vector<Node*> false_nodes_;
    tf::Task true_task_;
    tf::Task false_task_;
    tf::Task switch_task_;
};

EW_REGISTER_NODE(IfNode, "IfNode")

} // namespace easywork
