# Graph IR Schema v1 (JSON)

Current schema version: `1`

This document describes the strict production contract used by runtime loader, validator, and exporter.

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
- `from.method` (string)
- `to.node_id` (string)
- `to.method` (string)
- `to.arg_idx` (int)

Notes:
- `from.method` / `to.method` must be method names.
- Legacy `method_id` fields are rejected.

## `mux[]`

- `consumer_id` (string)
- `method` (string)
- `arg_idx` (int)
- `control_id` (string)
- `map` (object): choice -> producer node id

## `method_config[]`

- `node_id` (string)
- optional `order` (array of method names)
- optional `method` + `queue_size` (int)

Notes:
- `method_config.sync` was removed and is rejected.

## Runtime Constraints

- Runtime loader requires `schema_version == 1`.
- `Connect` validates producer/consumer method existence and consumer `arg_idx` bounds.
- Python-only nodes are not exportable.
- Constructor args/kwargs are limited to `bool/int/float/string`.

## Producer/Consumer APIs

- Python export API: `Pipeline.export(path)`.
- C++ load API: `easywork::GraphBuild::FromJsonFile(path)`.
- Runtime CLI: `easywork-run --graph <path>`.

## Tooling

- Production validator: `scripts/check_production_graph.py`
  - requires method-name edges
  - rejects removed `sync` config
- Migration tool: `scripts/migrate_graph_ir_v1.py`
  - converts legacy `method_id -> method`
  - removes removed `method_config.sync`

