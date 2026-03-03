#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/dev-gate}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"
DEV_GATE_INSTALL_PY_DEPS="${DEV_GATE_INSTALL_PY_DEPS:-1}"
DEV_GATE_SKIP_ABORT_TEST="${DEV_GATE_SKIP_ABORT_TEST:-1}"

echo "[dev_gate] root=$ROOT_DIR"
echo "[dev_gate] build_dir=$BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$CMAKE_GENERATOR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DEASYWORK_BUILD_PYTHON=ON \
  -DEASYWORK_WITH_OPENCV=ON \
  -DEASYWORK_BUILD_EXAMPLES=ON \
  -DEASYWORK_BUILD_TESTS=ON \
  -DEASYWORK_FETCH_DEPS=ON

cmake --build "$BUILD_DIR" -j

if [[ "$DEV_GATE_INSTALL_PY_DEPS" == "1" ]]; then
  "$PYTHON_BIN" -m pip install -r "$ROOT_DIR/python/requirements.txt"
fi

if [[ "$DEV_GATE_SKIP_ABORT_TEST" == "1" ]]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure -E PythonTests
  PYTEST_ARGS=(--ignore "$ROOT_DIR/tests/test_error_policy.py")
else
  ctest --test-dir "$BUILD_DIR" --output-on-failure
  PYTEST_ARGS=()
fi

PYTHONPATH="$ROOT_DIR/python" "$PYTHON_BIN" -m pytest "$ROOT_DIR/tests" "${PYTEST_ARGS[@]}"

echo "[dev_gate] PASS"
