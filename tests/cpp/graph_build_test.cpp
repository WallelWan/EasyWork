#include <any>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/core/graph_build.h"
#include "modules/control_nodes.h"
#include "modules/example_typed_nodes.h"

int main() {
    using easywork::GraphBuild;

    easywork::ResetMethodDispatchCounts();

    GraphBuild graph;

    const std::string src = graph.AddNode(
        "NumberSource",
        {std::any(0), std::any(5), std::any(1)},
        {}
    );

    const std::string recorder = graph.AddNode("MethodDispatchRecorder");

    graph.Connect(src, "forward", recorder, "left", 0);
    graph.Connect(src, "forward", recorder, "right", 0);
    graph.Connect(src, "forward", recorder, "forward", 0);
    graph.SetMethodOrder(recorder, {"left", "right", "forward"});

    graph.Build();
    graph.Open();
    graph.Close();

    graph.Reset();
    graph.Run();
    graph.Close();

    const int forward_count = easywork::GetMethodDispatchForwardCount();
    const int order_errors = easywork::GetMethodDispatchOrderErrorCount();

    if (forward_count <= 0) {
        std::cerr << "GraphBuild test failed: no forward calls" << std::endl;
        return 1;
    }
    if (order_errors != 0) {
        std::cerr << "GraphBuild test failed: method order errors: " << order_errors << std::endl;
        return 1;
    }

    std::cout << "GraphBuild C++ test passed. Forward calls: " << forward_count << std::endl;
    return 0;
}
