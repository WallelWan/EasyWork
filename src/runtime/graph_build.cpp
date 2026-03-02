#include "runtime/core/graph_build.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "runtime/registry/node_registry.h"

namespace easywork {
namespace {

std::any JsonToAny(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return std::any(value.get<bool>());
    }
    if (value.is_number_integer()) {
        return std::any(value.get<int64_t>());
    }
    if (value.is_number_float()) {
        return std::any(value.get<double>());
    }
    if (value.is_string()) {
        return std::any(value.get<std::string>());
    }
    throw std::runtime_error("Unsupported JSON argument type");
}

std::string ReadMethodName(const nlohmann::json& obj) {
    if (obj.contains("method")) {
        return obj.at("method").get<std::string>();
    }
    if (obj.contains("method_id")) {
        return obj.at("method_id").get<std::string>();
    }
    throw std::runtime_error("Missing method name in graph spec");
}

} // namespace

GraphBuild::GraphBuild() = default;

std::string GraphBuild::AddNode(const std::string& type,
                                const std::vector<std::any>& args,
                                const std::unordered_map<std::string, std::any>& kwargs) {
    std::string node_id = "n" + std::to_string(next_id_++);
    AddNodeWithId(node_id, type, args, kwargs);
    return node_id;
}

void GraphBuild::AddNodeWithId(const std::string& node_id,
                               const std::string& type,
                               const std::vector<std::any>& args,
                               const std::unordered_map<std::string, std::any>& kwargs) {
    if (nodes_.count(node_id)) {
        throw std::runtime_error("Duplicate node id: " + node_id);
    }
    auto node = NodeRegistry::instance().CreateAny(type, args, kwargs);
    nodes_.emplace(node_id, node);
    nodes_in_order_.push_back(node);
}

void GraphBuild::Connect(const std::string& from_id, const std::string& from_method,
                         const std::string& to_id, const std::string& to_method,
                         int arg_idx) {
    auto from_node = GetNode(from_id);
    auto to_node = GetNode(to_id);
    if (!from_node || !to_node) {
        throw std::runtime_error("Connect failed: node id not found");
    }
    (void)from_method;
    to_node->set_input_for(to_method, from_node.get(), arg_idx);
}

void GraphBuild::SetInputMux(const std::string& consumer_id, const std::string& method,
                             int arg_idx, const std::string& control_id,
                             const std::unordered_map<int, std::string>& map) {
    auto consumer = GetNode(consumer_id);
    auto control = GetNode(control_id);
    if (!consumer || !control) {
        throw std::runtime_error("Mux setup failed: node id not found");
    }
    std::unordered_map<int, Node*> raw_map;
    for (const auto& [choice, node_id] : map) {
        auto producer = GetNode(node_id);
        if (!producer) {
            throw std::runtime_error("Mux setup failed: producer not found");
        }
        consumer->set_weak_input(producer.get(), arg_idx);
        raw_map.emplace(choice, producer.get());
    }
    consumer->SetInputMux(method, arg_idx, control.get(), raw_map);
}

void GraphBuild::SetMethodOrder(const std::string& node_id, const std::vector<std::string>& order) {
    auto node = GetNode(node_id);
    if (!node) {
        throw std::runtime_error("Method order failed: node id not found");
    }
    node->SetMethodOrder(order);
}

void GraphBuild::SetMethodSync(const std::string& node_id, const std::string& method, bool enabled) {
    auto node = GetNode(node_id);
    if (!node) {
        throw std::runtime_error("Method sync failed: node id not found");
    }
    node->SetMethodSync(method, enabled);
}

void GraphBuild::SetMethodQueueSize(const std::string& node_id, const std::string& method, size_t max_queue) {
    auto node = GetNode(node_id);
    if (!node) {
        throw std::runtime_error("Method queue size failed: node id not found");
    }
    node->SetMethodQueueSize(method, max_queue);
}

void GraphBuild::Build() {
    BuildGraph();
}

void GraphBuild::Open() {
    std::vector<Node*> raw_nodes;
    raw_nodes.reserve(nodes_in_order_.size());
    for (const auto& node : nodes_in_order_) {
        raw_nodes.push_back(node.get());
    }
    executor_.open(raw_nodes);
}

void GraphBuild::Close() {
    std::vector<Node*> raw_nodes;
    raw_nodes.reserve(nodes_in_order_.size());
    for (const auto& node : nodes_in_order_) {
        raw_nodes.push_back(node.get());
    }
    executor_.close(raw_nodes);
}

void GraphBuild::Run() {
    BuildGraph();
    Open();
    executor_.run(graph_);
}

void GraphBuild::Reset() {
    graph_.Reset();
    built_ = false;
}

std::unique_ptr<GraphBuild> GraphBuild::FromJsonFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open graph spec file: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return FromJsonString(buffer.str());
}

std::unique_ptr<GraphBuild> GraphBuild::FromJsonString(const std::string& content) {
    auto build = std::make_unique<GraphBuild>();
    nlohmann::json spec = nlohmann::json::parse(content);

    if (!spec.contains("nodes") || !spec.at("nodes").is_array()) {
        throw std::runtime_error("Graph spec missing nodes array");
    }

    for (const auto& node : spec.at("nodes")) {
        std::string node_id = node.at("id").get<std::string>();
        std::string type = node.at("type").get<std::string>();
        std::vector<std::any> args;
        if (node.contains("args")) {
            for (const auto& arg : node.at("args")) {
                args.emplace_back(JsonToAny(arg));
            }
        }
        std::unordered_map<std::string, std::any> kwargs;
        if (node.contains("kwargs")) {
            for (const auto& item : node.at("kwargs").items()) {
                kwargs.emplace(item.key(), JsonToAny(item.value()));
            }
        }
        build->AddNodeWithId(node_id, type, args, kwargs);
    }

    if (spec.contains("edges")) {
        for (const auto& edge : spec.at("edges")) {
            const auto& from = edge.at("from");
            const auto& to = edge.at("to");
            std::string from_id = from.at("node_id").get<std::string>();
            std::string to_id = to.at("node_id").get<std::string>();
            std::string from_method = ReadMethodName(from);
            std::string to_method = ReadMethodName(to);
            int arg_idx = to.at("arg_idx").get<int>();
            build->Connect(from_id, from_method, to_id, to_method, arg_idx);
        }
    }

    if (spec.contains("mux")) {
        for (const auto& entry : spec.at("mux")) {
            std::string consumer_id = entry.at("consumer_id").get<std::string>();
            std::string method = entry.at("method").get<std::string>();
            int arg_idx = entry.at("arg_idx").get<int>();
            std::string control_id = entry.at("control_id").get<std::string>();
            std::unordered_map<int, std::string> map;
            for (const auto& item : entry.at("map").items()) {
                map.emplace(std::stoi(item.key()), item.value().get<std::string>());
            }
            build->SetInputMux(consumer_id, method, arg_idx, control_id, map);
        }
    }

    if (spec.contains("method_config")) {
        for (const auto& entry : spec.at("method_config")) {
            std::string node_id = entry.at("node_id").get<std::string>();
            if (entry.contains("order")) {
                std::vector<std::string> order = entry.at("order").get<std::vector<std::string>>();
                build->SetMethodOrder(node_id, order);
            }
            if (entry.contains("sync")) {
                std::string method = entry.at("method").get<std::string>();
                bool enabled = entry.at("sync").get<bool>();
                build->SetMethodSync(node_id, method, enabled);
            }
            if (entry.contains("queue_size")) {
                std::string method = entry.at("method").get<std::string>();
                size_t max_queue = entry.at("queue_size").get<size_t>();
                build->SetMethodQueueSize(node_id, method, max_queue);
            }
        }
    }

    return build;
}

void GraphBuild::BuildGraph() {
    if (built_) {
        return;
    }
    for (const auto& node : nodes_in_order_) {
        node->build(graph_);
    }
    for (const auto& node : nodes_in_order_) {
        node->connect();
    }
    for (const auto& node : nodes_in_order_) {
        node->Activate();
    }
    built_ = true;
}

std::shared_ptr<Node> GraphBuild::GetNode(const std::string& node_id) const {
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return nullptr;
    }
    return it->second;
}

} // namespace easywork
