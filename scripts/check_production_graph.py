#!/usr/bin/env python3
"""Validate EasyWork GraphSpec for production runtime usage."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

PRIMITIVE_TYPES = (bool, int, float, str)
ALLOWED_ROOT_KEYS = {"schema_version", "metadata", "nodes", "edges", "mux", "method_config"}
PYTHON_NODE_MARKERS = ("Python", "PyNode", "_Py")


def _is_primitive(value: Any) -> bool:
    return isinstance(value, PRIMITIVE_TYPES)


def _fail(errors: list[str], message: str) -> None:
    errors.append(message)


def _check_node_type(node_type: str, errors: list[str], *, allow_nodes: set[str]) -> None:
    if node_type in allow_nodes:
        return
    if not node_type:
        _fail(errors, "node.type must be a non-empty string")
        return
    if node_type.startswith("_"):
        _fail(errors, f"node.type '{node_type}' looks like an internal helper node")
        return
    if node_type.startswith("Py"):
        _fail(errors, f"node.type '{node_type}' is not allowed in production graph")
        return
    for marker in PYTHON_NODE_MARKERS:
        if marker in node_type:
            _fail(errors, f"node.type '{node_type}' is not allowed in production graph")
            return


def validate_graph_spec(data: dict[str, Any], *, expected_schema: int, allow_nodes: set[str]) -> list[str]:
    errors: list[str] = []

    unknown_root = set(data.keys()) - ALLOWED_ROOT_KEYS
    if unknown_root:
        _fail(errors, f"Unknown root keys: {sorted(unknown_root)}")

    if "schema_version" not in data:
        _fail(errors, "Missing required key: schema_version")
    elif data["schema_version"] != expected_schema:
        _fail(errors, f"schema_version must be {expected_schema}, got {data['schema_version']}")

    nodes = data.get("nodes")
    if not isinstance(nodes, list):
        _fail(errors, "nodes must be an array")
        nodes = []

    edges = data.get("edges")
    if not isinstance(edges, list):
        _fail(errors, "edges must be an array")
        edges = []

    node_ids: set[str] = set()
    for idx, node in enumerate(nodes):
        prefix = f"nodes[{idx}]"
        if not isinstance(node, dict):
            _fail(errors, f"{prefix} must be an object")
            continue
        node_id = node.get("id")
        node_type = node.get("type")
        args = node.get("args", [])
        kwargs = node.get("kwargs", {})

        if not isinstance(node_id, str) or not node_id:
            _fail(errors, f"{prefix}.id must be a non-empty string")
        elif node_id in node_ids:
            _fail(errors, f"Duplicate node id: {node_id}")
        else:
            node_ids.add(node_id)

        if not isinstance(node_type, str):
            _fail(errors, f"{prefix}.type must be a string")
        else:
            _check_node_type(node_type, errors, allow_nodes=allow_nodes)

        if not isinstance(args, list):
            _fail(errors, f"{prefix}.args must be an array")
        else:
            for arg_i, arg in enumerate(args):
                if not _is_primitive(arg):
                    _fail(errors, f"{prefix}.args[{arg_i}] must be bool/int/float/string")

        if not isinstance(kwargs, dict):
            _fail(errors, f"{prefix}.kwargs must be an object")
        else:
            for key, value in kwargs.items():
                if not isinstance(key, str):
                    _fail(errors, f"{prefix}.kwargs has non-string key")
                    continue
                if not _is_primitive(value):
                    _fail(errors, f"{prefix}.kwargs['{key}'] must be bool/int/float/string")

    for idx, edge in enumerate(edges):
        prefix = f"edges[{idx}]"
        if not isinstance(edge, dict):
            _fail(errors, f"{prefix} must be an object")
            continue
        from_obj = edge.get("from")
        to_obj = edge.get("to")
        if not isinstance(from_obj, dict) or not isinstance(to_obj, dict):
            _fail(errors, f"{prefix}.from and {prefix}.to must be objects")
            continue

        from_id = from_obj.get("node_id")
        to_id = to_obj.get("node_id")
        if from_id not in node_ids:
            _fail(errors, f"{prefix}.from.node_id '{from_id}' not found in nodes")
        if to_id not in node_ids:
            _fail(errors, f"{prefix}.to.node_id '{to_id}' not found in nodes")

        from_method = from_obj.get("method", from_obj.get("method_id"))
        to_method = to_obj.get("method", to_obj.get("method_id"))
        if not isinstance(from_method, str) or not from_method:
            _fail(errors, f"{prefix}.from.method/method_id must be a non-empty string")
        if not isinstance(to_method, str) or not to_method:
            _fail(errors, f"{prefix}.to.method/method_id must be a non-empty string")

        arg_idx = to_obj.get("arg_idx")
        if not isinstance(arg_idx, int) or arg_idx < 0:
            _fail(errors, f"{prefix}.to.arg_idx must be a non-negative int")

    mux_entries = data.get("mux", [])
    if not isinstance(mux_entries, list):
        _fail(errors, "mux must be an array when provided")
        mux_entries = []

    for idx, entry in enumerate(mux_entries):
        prefix = f"mux[{idx}]"
        if not isinstance(entry, dict):
            _fail(errors, f"{prefix} must be an object")
            continue
        consumer_id = entry.get("consumer_id")
        control_id = entry.get("control_id")
        method = entry.get("method")
        arg_idx = entry.get("arg_idx")
        mapping = entry.get("map")

        if consumer_id not in node_ids:
            _fail(errors, f"{prefix}.consumer_id '{consumer_id}' not found in nodes")
        if control_id not in node_ids:
            _fail(errors, f"{prefix}.control_id '{control_id}' not found in nodes")
        if not isinstance(method, str) or not method:
            _fail(errors, f"{prefix}.method must be a non-empty string")
        if not isinstance(arg_idx, int) or arg_idx < 0:
            _fail(errors, f"{prefix}.arg_idx must be a non-negative int")
        if not isinstance(mapping, dict) or not mapping:
            _fail(errors, f"{prefix}.map must be a non-empty object")
            continue
        for key, producer_id in mapping.items():
            try:
                int(key)
            except Exception:
                _fail(errors, f"{prefix}.map key '{key}' is not an int-like string")
            if producer_id not in node_ids:
                _fail(errors, f"{prefix}.map producer '{producer_id}' not found in nodes")

    method_cfg = data.get("method_config", [])
    if not isinstance(method_cfg, list):
        _fail(errors, "method_config must be an array when provided")
        method_cfg = []

    for idx, entry in enumerate(method_cfg):
        prefix = f"method_config[{idx}]"
        if not isinstance(entry, dict):
            _fail(errors, f"{prefix} must be an object")
            continue
        node_id = entry.get("node_id")
        if node_id not in node_ids:
            _fail(errors, f"{prefix}.node_id '{node_id}' not found in nodes")
        if "order" in entry:
            order = entry["order"]
            if not isinstance(order, list) or not all(isinstance(x, str) and x for x in order):
                _fail(errors, f"{prefix}.order must be string array")
        if "sync" in entry:
            if "method" not in entry or not isinstance(entry["method"], str):
                _fail(errors, f"{prefix}.method must be string when sync is set")
            if not isinstance(entry["sync"], bool):
                _fail(errors, f"{prefix}.sync must be bool")
        if "queue_size" in entry:
            if "method" not in entry or not isinstance(entry["method"], str):
                _fail(errors, f"{prefix}.method must be string when queue_size is set")
            if not isinstance(entry["queue_size"], int) or entry["queue_size"] < 0:
                _fail(errors, f"{prefix}.queue_size must be non-negative int")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate production-safe EasyWork graph JSON")
    parser.add_argument("--graph", required=True, type=Path, help="Path to GraphSpec JSON")
    parser.add_argument("--schema-version", type=int, default=1, help="Expected schema version")
    parser.add_argument(
        "--allow-node",
        action="append",
        default=[],
        help="Allowlisted node type (can be repeated)",
    )
    args = parser.parse_args()

    try:
        with args.graph.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"[FAIL] graph file not found: {args.graph}", file=sys.stderr)
        return 1
    except json.JSONDecodeError as exc:
        print(f"[FAIL] invalid JSON: {exc}", file=sys.stderr)
        return 1

    if not isinstance(data, dict):
        print("[FAIL] graph root must be an object", file=sys.stderr)
        return 1

    errors = validate_graph_spec(
        data,
        expected_schema=args.schema_version,
        allow_nodes=set(args.allow_node),
    )
    if errors:
        print("[FAIL] production graph validation failed:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    print(f"[OK] production graph validation passed: {args.graph}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
