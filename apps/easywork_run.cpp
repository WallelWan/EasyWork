#include <exception>
#include <iostream>
#include <string>

#include "modules/module_registry.h"
#include "runtime/core/graph_build.h"

int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "--graph") {
        std::cerr << "Usage: easywork-run --graph <pipeline.json>" << std::endl;
        return 2;
    }

    try {
        auto build = easywork::GraphBuild::FromJsonFile(argv[2]);
        build->Run();
        build->Close();
        std::cout << "graph run completed: " << argv[2] << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "easywork-run failed: " << e.what() << std::endl;
        return 1;
    }
}
