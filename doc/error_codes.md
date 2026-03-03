# Runtime Error Codes and Failure Semantics

EasyWork runtime uses stable error codes for logs, exceptions, and status inspection.

## Error Codes

- `EW_OK`
- `EW_RUNTIME_ERROR`
- `EW_DISPATCH_ERROR`
- `EW_PY_DISPATCH_ERROR`
- `EW_IFNODE_ERROR`
- `EW_GRAPH_SPEC_INVALID`
- `EW_GRAPH_NODE_NOT_FOUND`
- `EW_GRAPH_CONNECT_ERROR`
- `EW_GRAPH_MUX_ERROR`
- `EW_RUNNER_USAGE_ERROR`
- `EW_RUNNER_CONFIG_ERROR`
- `EW_RUNNER_RUNTIME_ERROR`

## Runner Exit Semantics

- `0`: success (`EW_OK`)
- `2`: CLI usage error (`EW_RUNNER_USAGE_ERROR`)
- `1`: all other failures

## Runtime Failure Semantics

- `FailFast`: first dispatch/runtime error stops execution loop.
- `SkipCurrentData`: error is recorded, current data is dropped, execution continues.

Python-side accessors:

- `ExecutionGraph.last_error()`
- `ExecutionGraph.last_error_code()`
- `ExecutionGraph.last_error_code_name()`
- `ExecutionGraph.error_count()`

