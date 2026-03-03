#include <exception>
#include <iostream>
#include <string>

#include "modules/module_registry.h"
#include "runtime/core/error_codes.h"
#include "runtime/core/graph_build.h"
#include "runtime/core/logger.h"

namespace {

void PrintUsage() {
    std::cerr
        << "Usage: easywork-run --graph <pipeline.json> "
        << "[--log-level <trace|debug|info|warn|error>] "
        << "[--log-file <path>] "
        << "[--log-format <text|json>]" << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    std::string graph_path;
    std::string log_level = "info";
    std::string log_format = "text";
    std::string log_file;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--graph" && i + 1 < argc) {
            graph_path = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            log_level = argv[++i];
        } else if (arg == "--log-file" && i + 1 < argc) {
            log_file = argv[++i];
        } else if (arg == "--log-format" && i + 1 < argc) {
            log_format = argv[++i];
        } else {
            PrintUsage();
            return easywork::RunnerExitCode(easywork::ErrorCode::RunnerUsageError);
        }
    }

    if (graph_path.empty()) {
        PrintUsage();
        return easywork::RunnerExitCode(easywork::ErrorCode::RunnerUsageError);
    }

    try {
        easywork::RuntimeLogger::Instance().Configure(
            easywork::ParseRuntimeLogLevel(log_level),
            easywork::ParseRuntimeLogFormat(log_format),
            log_file
        );
        easywork::LogRuntime(easywork::RuntimeLogLevel::Info, "easywork-run started", {
            {"event", "runner_start"},
            {"graph_path", graph_path},
            {"log_level", log_level},
            {"log_format", log_format},
            {"log_file", log_file.empty() ? "stderr" : log_file},
        });
        auto build = easywork::GraphBuild::FromJsonFile(graph_path);
        build->Run();
        build->Close();
        easywork::LogRuntime(easywork::RuntimeLogLevel::Info, "easywork-run completed", {
            {"event", "runner_finish"},
            {"graph_path", graph_path},
        });
        std::cout << "graph run completed: " << graph_path << std::endl;
        return easywork::RunnerExitCode(easywork::ErrorCode::Ok);
    } catch (const std::exception& e) {
        const bool is_config_error =
            std::string(e.what()).find("Unknown log level") != std::string::npos ||
            std::string(e.what()).find("Unknown log format") != std::string::npos ||
            std::string(e.what()).find("Failed to open log file") != std::string::npos;
        const easywork::ErrorCode code =
            is_config_error ? easywork::ErrorCode::RunnerConfigError
                            : easywork::ErrorCode::RunnerRuntimeError;
        easywork::LogRuntime(easywork::RuntimeLogLevel::Error, "easywork-run failed", {
            {"event", "runner_error"},
            {"error_code", std::string(easywork::ErrorCodeName(code))},
            {"detail", e.what()},
        });
        std::cerr << "[" << easywork::ErrorCodeName(code) << "] easywork-run failed: " << e.what() << std::endl;
        return easywork::RunnerExitCode(code);
    }
}
