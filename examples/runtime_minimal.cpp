#include <any>
#include <iostream>
#include <string>

#include "modules/example_typed_nodes.h"
#include "runtime/core/graph_build.h"

int main() {
    try {
        easywork::GraphBuild graph;

        const std::string src = graph.AddNode("NumberSource", {std::any(1), std::any(3), std::any(1)}, {});
        const std::string mul = graph.AddNode("MultiplyBy", {std::any(2)}, {});

        graph.Connect(src, "forward", mul, "forward", 0);
        graph.Run();
        graph.Close();

        std::cout << "runtime example finished" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "runtime example failed: " << e.what() << std::endl;
        return 1;
    }
}
