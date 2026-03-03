#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRESET="${PRESET:-cross-aarch64-release}"
TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-aarch64-linux-gnu}"

echo "[cross_gate] root=$ROOT_DIR"
echo "[cross_gate] preset=$PRESET"
echo "[cross_gate] toolchain_prefix=$TOOLCHAIN_PREFIX"

if ! command -v "${TOOLCHAIN_PREFIX}-g++" >/dev/null 2>&1; then
  echo "[cross_gate] SKIP: compiler not found: ${TOOLCHAIN_PREFIX}-g++"
  exit 0
fi

VERSION_RAW="$("${TOOLCHAIN_PREFIX}-g++" -dumpfullversion -dumpversion || true)"
VERSION_MAJOR="${VERSION_RAW%%.*}"
if [[ -z "${VERSION_MAJOR}" || ! "${VERSION_MAJOR}" =~ ^[0-9]+$ ]]; then
  echo "[cross_gate] FAIL: unable to parse compiler version: ${VERSION_RAW}" >&2
  exit 1
fi
if (( VERSION_MAJOR < 10 )); then
  echo "[cross_gate] FAIL: compiler too old (${VERSION_RAW}), need GCC 10+" >&2
  exit 1
fi

cmake --preset "$PRESET"
cmake --build "$ROOT_DIR/build/cross-aarch64-release" -j

echo "[cross_gate] PASS"

