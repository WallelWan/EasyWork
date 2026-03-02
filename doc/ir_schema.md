# Graph IR Schema v0 (JSON)

Current schema version: `1`

## Root
```json
{
  "schema_version": 1,
  "metadata": {"easywork_version": "...", "export_time": "..."},
  "nodes": [],
  "edges": [],
  "mux": [],
  "method_config": []
}
```

## `nodes[]`
- `id` (string): unique node id, e.g. `n1`
- `type` (string): registered node type name
- `args` (array): positional constructor args (primitive JSON only)
- `kwargs` (object): keyword constructor args (primitive JSON only)

## `edges[]`
- `from.node_id` (string)
- `from.method` or `from.method_id` (string)
- `to.node_id` (string)
- `to.method` or `to.method_id` (string)
- `to.arg_idx` (int)

## `mux[]`
- `consumer_id` (string)
- `method` (string)
- `arg_idx` (int)
- `control_id` (string)
- `map` (object): choice -> producer node id

## `method_config[]`
- `node_id` (string)
- optional `order` (array of method names)
- optional `method` + `sync` (bool)
- optional `method` + `queue_size` (int)

## Constraints
- Python-only nodes are not exportable.
- Constructor args/kwargs are limited to `bool/int/float/string`.
- Runtime loader fails on unknown node ids/types or malformed edges.

## Producer/consumer APIs
- Python export APIs: `Pipeline.export(path)` and `Pipeline.export_graph(path)`.
- C++ load API: `easywork::GraphBuild::FromJsonFile(path)`.
- Runtime CLI: `easywork-run --graph <path>`.
