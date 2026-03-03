#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/prod-gate}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"
GRAPH_PATH="${GRAPH_PATH:-}"
ARTIFACT_DIR="${ARTIFACT_DIR:-}"

echo "[prod_gate] root=$ROOT_DIR"
echo "[prod_gate] build_dir=$BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$CMAKE_GENERATOR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DEASYWORK_BUILD_PYTHON=OFF \
  -DEASYWORK_WITH_OPENCV=OFF \
  -DEASYWORK_BUILD_EXAMPLES=ON \
  -DEASYWORK_BUILD_TESTS=ON \
  -DEASYWORK_FETCH_DEPS=OFF

cmake --build "$BUILD_DIR" -j
ctest --test-dir "$BUILD_DIR" --output-on-failure

if [[ -z "$GRAPH_PATH" ]]; then
  GRAPH_PATH="$BUILD_DIR/prod_gate_smoke_graph.json"
  cat > "$GRAPH_PATH" <<'JSON'
{
  "schema_version": 1,
  "metadata": {
    "easywork_version": "prod_gate_smoke",
    "export_time": "2026-03-03T00:00:00Z"
  },
  "nodes": [
    {"id": "n1", "type": "NumberSource", "args": [0, 2, 1], "kwargs": {}},
    {"id": "n2", "type": "MultiplyBy", "args": [2], "kwargs": {}}
  ],
  "edges": [
    {"from": {"node_id": "n1", "method": "forward"}, "to": {"node_id": "n2", "method": "forward", "arg_idx": 0}}
  ],
  "mux": [],
  "method_config": []
}
JSON
fi

"$PYTHON_BIN" "$ROOT_DIR/scripts/check_production_graph.py" --graph "$GRAPH_PATH"

"$BUILD_DIR/easywork-run" --graph "$GRAPH_PATH" --log-level info --log-format text

if [[ -n "$ARTIFACT_DIR" ]]; then
  "$ROOT_DIR/scripts/check_production_artifacts.sh" "$BUILD_DIR" "$ARTIFACT_DIR"
else
  "$ROOT_DIR/scripts/check_production_artifacts.sh" "$BUILD_DIR"
fi

echo "[prod_gate] PASS"
