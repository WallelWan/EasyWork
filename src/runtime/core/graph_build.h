#pragma once

#include <any>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "runtime/core/core.h"

namespace easywork {

class GraphBuild {
public:
    GraphBuild();

    std::string AddNode(const std::string& type,
                        const std::vector<std::any>& args = {},
                        const std::unordered_map<std::string, std::any>& kwargs = {});

    void AddNodeWithId(const std::string& node_id,
                       const std::string& type,
                       const std::vector<std::any>& args,
                       const std::unordered_map<std::string, std::any>& kwargs);

    void Connect(const std::string& from_id, const std::string& from_method,
                 const std::string& to_id, const std::string& to_method,
                 int arg_idx);

    void SetInputMux(const std::string& consumer_id, const std::string& method,
                     int arg_idx, const std::string& control_id,
                     const std::unordered_map<int, std::string>& map);

    void SetMethodOrder(const std::string& node_id, const std::vector<std::string>& order);
    void SetMethodSync(const std::string& node_id, const std::string& method, bool enabled);
    void SetMethodQueueSize(const std::string& node_id, const std::string& method, size_t max_queue);

    void Build();
    void Open();
    void Close();
    void Run();
    void Reset();

    static std::unique_ptr<GraphBuild> FromJsonFile(const std::string& path);
    static std::unique_ptr<GraphBuild> FromJsonString(const std::string& content);

private:
    ExecutionGraph graph_;
    Executor executor_;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes_;
    std::vector<std::shared_ptr<Node>> nodes_in_order_;
    bool built_{false};
    size_t next_id_{1};

    void BuildGraph();
    std::shared_ptr<Node> GetNode(const std::string& node_id) const;
};

} // namespace easywork
