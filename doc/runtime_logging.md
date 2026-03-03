# Runtime Logging

EasyWork runtime logging is configured through `easywork-run`:

```bash
easywork-run --graph graph.json \
  --log-level info \
  --log-format text \
  --log-file /tmp/easywork.log
```

## CLI Options

- `--log-level`: `trace|debug|info|warn|error`
- `--log-format`: `text|json`
- `--log-file`: optional path; defaults to `stderr`

## Required Log Fields

The runtime logger carries the following fields for observability:

- `time`
- `level`
- `node` (when available)
- `method` (when available)
- `error_code` (for errors)
- `trace_id` (per-process trace id)

Additional event-specific fields are emitted (for example `event`, `graph_path`, `error_count`, `elapsed_ms`).

Error code taxonomy is documented in `doc/error_codes.md`.

## Key Runtime Events

- Graph load / parse
- Node create / connect
- Graph build start / finish
- Dispatch exception
- Stop reason and executor summary
