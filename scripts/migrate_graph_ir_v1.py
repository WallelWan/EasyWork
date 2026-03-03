#!/usr/bin/env python3
"""Migrate legacy EasyWork GraphSpec JSON to strict v1 schema.

This script performs deterministic migrations:
- edges.from.method_id -> edges.from.method
- edges.to.method_id -> edges.to.method
- drops method_config entries containing legacy "sync"
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def _migrate_edge_method(obj: dict[str, Any], path: str) -> None:
    method = obj.get("method")
    legacy = obj.get("method_id")
    if method is None and legacy is not None:
        obj["method"] = legacy
    if "method_id" in obj:
        del obj["method_id"]
    if not isinstance(obj.get("method"), str) or not obj["method"]:
        raise ValueError(f"{path}.method must be a non-empty string after migration")


def migrate_graph(data: dict[str, Any]) -> tuple[dict[str, Any], list[str]]:
    notes: list[str] = []

    edges = data.get("edges", [])
    if not isinstance(edges, list):
        raise ValueError("edges must be an array")
    for idx, edge in enumerate(edges):
        if not isinstance(edge, dict):
            raise ValueError(f"edges[{idx}] must be an object")
        from_obj = edge.get("from")
        to_obj = edge.get("to")
        if not isinstance(from_obj, dict) or not isinstance(to_obj, dict):
            raise ValueError(f"edges[{idx}].from and edges[{idx}].to must be objects")
        if "method_id" in from_obj or "method_id" in to_obj:
            notes.append(f"converted edges[{idx}] method_id -> method")
        _migrate_edge_method(from_obj, f"edges[{idx}].from")
        _migrate_edge_method(to_obj, f"edges[{idx}].to")

    method_cfg = data.get("method_config", [])
    if not isinstance(method_cfg, list):
        raise ValueError("method_config must be an array")
    migrated_cfg: list[dict[str, Any]] = []
    for idx, entry in enumerate(method_cfg):
        if not isinstance(entry, dict):
            raise ValueError(f"method_config[{idx}] must be an object")
        if "sync" in entry:
            notes.append(f"removed unsupported method_config[{idx}].sync entry")
            continue
        migrated_cfg.append(entry)
    data["method_config"] = migrated_cfg

    if "schema_version" not in data:
        data["schema_version"] = 1
        notes.append("filled missing schema_version=1")
    if data.get("schema_version") != 1:
        raise ValueError("only schema_version 1 is supported by this migration")

    return data, notes


def main() -> int:
    parser = argparse.ArgumentParser(description="Migrate EasyWork GraphSpec to strict v1 schema")
    parser.add_argument("--input", required=True, type=Path, help="Input graph json path")
    parser.add_argument("--output", required=True, type=Path, help="Output graph json path")
    args = parser.parse_args()

    try:
        with args.input.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"[FAIL] input file not found: {args.input}", file=sys.stderr)
        return 1
    except json.JSONDecodeError as exc:
        print(f"[FAIL] invalid JSON: {exc}", file=sys.stderr)
        return 1

    if not isinstance(data, dict):
        print("[FAIL] graph root must be an object", file=sys.stderr)
        return 1

    try:
        migrated, notes = migrate_graph(data)
    except ValueError as exc:
        print(f"[FAIL] migration error: {exc}", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        json.dump(migrated, f, ensure_ascii=True, indent=2)
        f.write("\n")

    print(f"[OK] migrated graph written: {args.output}")
    for note in notes:
        print(f"  - {note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
