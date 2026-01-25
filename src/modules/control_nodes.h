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
            std::cerr << "IfNode error: " << e.what() << std::endl;
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

class EvenCheck : public BaseNode<EvenCheck> {
public:
    bool forward(int input) {
        return (input % 2) == 0;
    }

    EW_ENABLE_METHODS(forward)
};

class MergeNode : public Node {
public:
    void build(ExecutionGraph& g) override {
        graph_ = &g;
        task_ = g.taskflow.emplace([this]() { RunMerge(); });
        task_.name("MergeNode");
    }

    void connect() override {
        std::unordered_set<Node*> predecessors;
        for (const auto& conn : upstreams_) {
            if (conn.node) {
                predecessors.insert(conn.node);
            }
        }

        for (Node* node : predecessors) {
            node->get_task().precede(task_);
        }

        output_type_ = ResolveOutputType();
        output_type_set_ = true;
    }

    NodeTypeInfo get_type_info() const override {
        NodeTypeInfo info;
        MethodInfo method;
        method.output_type = output_type_set_ ? output_type_ : TypeInfo::create<void>();
        info.methods[ID_FORWARD] = method;
        return info;
    }

    std::string type_name() const override {
        return "MergeNode";
    }

    std::vector<std::string> exposed_methods() const override {
        return {"forward"};
    }

private:
    TypeInfo ResolveOutputType() const {
        TypeInfo resolved = TypeInfo::create<void>();
        bool resolved_set = false;
        for (const auto& conn : upstreams_) {
            if (!conn.node) {
                continue;
            }
            NodeTypeInfo info = conn.node->get_type_info();
            auto it = info.methods.find(ID_FORWARD);
            if (it == info.methods.end()) {
                continue;
            }
            if (!resolved_set) {
                resolved = it->second.output_type;
                resolved_set = true;
                continue;
            }
            if (resolved != it->second.output_type) {
                throw std::runtime_error("MergeNode input type mismatch");
            }
        }
        return resolved;
    }

    void RunMerge() {
        try {
            EnsurePortBufferSize();
            BufferPortInputs();
            for (auto& buffer : port_buffers_) {
                if (!buffer.empty()) {
                    output_packet_ = std::move(buffer.front());
                    buffer.pop_front();
                    return;
                }
            }

            output_packet_ = Packet::Empty();
        } catch (const std::exception& e) {
            std::cerr << "MergeNode error: " << e.what() << std::endl;
            output_packet_ = Packet::Empty();
        }
    }

    TypeInfo output_type_{};
    bool output_type_set_{false};
};

EW_REGISTER_NODE(IfNode, "IfNode")
EW_REGISTER_NODE(EvenCheck, "EvenCheck")
EW_REGISTER_NODE(MergeNode, "MergeNode")

} // namespace easywork
