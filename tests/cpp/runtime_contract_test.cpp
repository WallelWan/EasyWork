#include <any>
#include <iostream>
#include <string>
#include <vector>

#include "modules/example_typed_nodes.h"
#include "runtime/core/graph_build.h"
#include "runtime/types/type_system.h"

namespace {

int TestPacketTypeContract() {
    auto p = easywork::Packet::From(42, 123);
    if (!p.has_value()) {
        std::cerr << "Packet contract: payload missing" << std::endl;
        return 1;
    }
    if (!(p.type() == easywork::TypeInfo::create<int>())) {
        std::cerr << "Packet contract: type mismatch" << std::endl;
        return 1;
    }
    if (p.cast<int>() != 42) {
        std::cerr << "Packet contract: cast mismatch" << std::endl;
        return 1;
    }
    return 0;
}

int TestGraphBuilderContract() {
    easywork::GraphBuild graph;

    const std::string src = graph.AddNode("NumberSource", {std::any(0), std::any(2), std::any(1)}, {});
    const std::string mul = graph.AddNode("MultiplyBy", {std::any(3)}, {});

    graph.Connect(src, "forward", mul, "forward", 0);
    graph.Run();
    graph.Close();

    return 0;
}

int TestIrValidationFailure() {
    try {
        const char* bad_spec = R"json({
          "schema_version": 1,
          "nodes": [
            {"id": "n1", "type": "NumberSource", "args": [0, 1, 1], "kwargs": {}}
          ],
          "edges": [
            {
              "from": {"node_id": "n_missing", "method": "forward"},
              "to": {"node_id": "n1", "method": "forward", "arg_idx": 0}
            }
          ]
        })json";
        auto g = easywork::GraphBuild::FromJsonString(bad_spec);
        (void)g;
        std::cerr << "IR validation contract: expected failure did not happen" << std::endl;
        return 1;
    } catch (const std::exception&) {
        return 0;
    }
}

int TestIrLegacyMethodIdRejected() {
    try {
        const char* bad_spec = R"json({
          "schema_version": 1,
          "nodes": [
            {"id": "n1", "type": "NumberSource", "args": [0, 1, 1], "kwargs": {}},
            {"id": "n2", "type": "MultiplyBy", "args": [2], "kwargs": {}}
          ],
          "edges": [
            {
              "from": {"node_id": "n1", "method_id": "forward"},
              "to": {"node_id": "n2", "method": "forward", "arg_idx": 0}
            }
          ]
        })json";
        auto g = easywork::GraphBuild::FromJsonString(bad_spec);
        (void)g;
        std::cerr << "IR legacy method_id contract: expected failure did not happen" << std::endl;
        return 1;
    } catch (const std::exception&) {
        return 0;
    }
}

int TestIrMethodSyncRejected() {
    try {
        const char* bad_spec = R"json({
          "schema_version": 1,
          "nodes": [
            {"id": "n1", "type": "NumberSource", "args": [0, 1, 1], "kwargs": {}}
          ],
          "edges": [],
          "method_config": [
            {"node_id": "n1", "method": "forward", "sync": true}
          ]
        })json";
        auto g = easywork::GraphBuild::FromJsonString(bad_spec);
        (void)g;
        std::cerr << "IR method_config.sync contract: expected failure did not happen" << std::endl;
        return 1;
    } catch (const std::exception&) {
        return 0;
    }
}

}  // namespace

int main() {
    if (TestPacketTypeContract() != 0) {
        return 1;
    }
    if (TestGraphBuilderContract() != 0) {
        return 1;
    }
    if (TestIrValidationFailure() != 0) {
        return 1;
    }
    if (TestIrLegacyMethodIdRejected() != 0) {
        return 1;
    }
    if (TestIrMethodSyncRejected() != 0) {
        return 1;
    }

    std::cout << "runtime contract tests passed" << std::endl;
    return 0;
}
