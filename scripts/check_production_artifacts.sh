#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <artifact_dir> [artifact_dir...]" >&2
  exit 2
fi

fail=0
if command -v rg >/dev/null 2>&1; then
  FILTER_CMD=(rg -n 'easywork_core|python|site-packages')
else
  FILTER_CMD=(grep -En 'easywork_core|python|site-packages')
fi

for dir in "$@"; do
  if [[ ! -d "$dir" ]]; then
    echo "[artifact_check] WARN: directory does not exist, skip: $dir"
    continue
  fi
  if find "$dir" -type f \( -name 'easywork_core*' -o -name '*.pyd' -o -name '*.so' \) 2>/dev/null \
      | "${FILTER_CMD[@]}" >/dev/null 2>&1; then
    echo "[artifact_check] FAIL: python extension artifact found under: $dir" >&2
    find "$dir" -type f \( -name 'easywork_core*' -o -name '*.pyd' -o -name '*.so' \) 2>/dev/null \
      | "${FILTER_CMD[@]}" >&2 || true
    fail=1
  fi
done

if [[ $fail -ne 0 ]]; then
  exit 1
fi

echo "[artifact_check] PASS"
