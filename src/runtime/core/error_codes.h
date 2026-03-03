#pragma once

#include <cstdint>
#include <string_view>

namespace easywork {

enum class ErrorCode : std::uint32_t {
    Ok = 0,

    RuntimeError = 1000,
    DispatchError = 1001,
    PythonDispatchError = 1002,
    IfNodeError = 1003,

    GraphSpecInvalid = 1100,
    GraphNodeNotFound = 1101,
    GraphConnectError = 1102,
    GraphMuxError = 1103,

    RunnerUsageError = 1200,
    RunnerConfigError = 1201,
    RunnerRuntimeError = 1202,
};

inline std::string_view ErrorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return "EW_OK";
        case ErrorCode::RuntimeError:
            return "EW_RUNTIME_ERROR";
        case ErrorCode::DispatchError:
            return "EW_DISPATCH_ERROR";
        case ErrorCode::PythonDispatchError:
            return "EW_PY_DISPATCH_ERROR";
        case ErrorCode::IfNodeError:
            return "EW_IFNODE_ERROR";
        case ErrorCode::GraphSpecInvalid:
            return "EW_GRAPH_SPEC_INVALID";
        case ErrorCode::GraphNodeNotFound:
            return "EW_GRAPH_NODE_NOT_FOUND";
        case ErrorCode::GraphConnectError:
            return "EW_GRAPH_CONNECT_ERROR";
        case ErrorCode::GraphMuxError:
            return "EW_GRAPH_MUX_ERROR";
        case ErrorCode::RunnerUsageError:
            return "EW_RUNNER_USAGE_ERROR";
        case ErrorCode::RunnerConfigError:
            return "EW_RUNNER_CONFIG_ERROR";
        case ErrorCode::RunnerRuntimeError:
            return "EW_RUNNER_RUNTIME_ERROR";
    }
    return "EW_UNKNOWN_ERROR";
}

inline int RunnerExitCode(ErrorCode code) {
    switch (code) {
        case ErrorCode::RunnerUsageError:
            return 2;
        case ErrorCode::Ok:
            return 0;
        default:
            return 1;
    }
}

} // namespace easywork

