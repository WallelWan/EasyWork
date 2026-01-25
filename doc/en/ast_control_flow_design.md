# EasyWork AST Control Flow Design (Strict Mode)

## Goals
- Allow users to write native Python `if/else` in graph construction.
- Translate control flow into EasyWork's Taskflow conditional execution.
- Enforce strict graph-only logic inside `Pipeline.construct()`.
- Provide construction-time errors for potentially undefined variables.

## Scope
- Automatic AST transform: `Pipeline.construct()`
- Manual AST transform: `@ew.ast_transform`

## AST Library
- Use `gast` for uniform AST across Python versions.

## AST Entry Points
- `Pipeline.construct()` is transformed automatically before graph build.
- `@ew.ast_transform` decorator applies the same transformation to other functions.

## Pythonic Internal DSL
- `ew.if_(cond)` returns `BranchCtx`.
- `BranchCtx.true` / `BranchCtx.false` contexts.
- `BranchCtx.assign(name, value)` records branch assignment.
- `BranchCtx.value(name)` produces merged output after the if.

## Implementation Notes
- Runtime branching is implemented with C++ control nodes:
  - `IfNode` creates a Taskflow condition task and routes to true/false branches.
  - Input Multiplexing (`SetInputMux`) handles merging selected branch outputs back into a single stream at the consumer node.
- The AST rewrite emits `if_` + branch contexts and injects assignments to drive
  the multiplexing logic automatically.

## Transformation Template
User code:

```python
if cond:
    y = node_a(x)
else:
    y = node_b(x)
z = node_c(y)
```

AST rewrite:

```python
with ew.if_(cond) as br:
    with br.true:
        y = node_a(x)
        br.assign("y", y)
    with br.false:
        y = node_b(x)
        br.assign("y", y)

y = br.value("y")
z = node_c(y)
```

## Variable Flow Rules
- Maintain a `defined` set for each scope.
- For `if`:
  - Compute `defined_true`, `defined_false`.
  - `defined_out = defined_true ∩ defined_false`.
  - `maybe_undef = (defined_true ∪ defined_false) - defined_out`.
- Any variable read after `if` that is in `maybe_undef` triggers a
  construction-time error.

## Condition Type Rules

- `if` conditions must resolve to `bool` or `int` (including int64).
- Invalid condition types are rejected during graph construction.

## Nested If Support

- Nested `if/else` is supported via a branch context stack.
- All nodes created inside a nested branch are registered to each active branch scope.

## Strict Graph-Only Construct
Allowed inside `construct()`:
- Node creation.
- Node call / method call.
- `if/else` with graph condition.
- Assignment of graph outputs.

Forbidden:
- Pure Python computations (`a = 1 + 2`).
- IO / print.
- `return`, `break`, `continue`, `yield`.
- Comprehensions / dict/list literals not from graph nodes.
- Any expression not recognized as a graph expression.

## Graph Expression Recognition (Strict)
`is_graph_expr(expr)` must be true for any assignment RHS.
Strict acceptance:
- Direct call to node wrapper / module factory.
- Call chain that resolves to EasyWork node output.
- Any expression containing only EasyWork Symbol/Node output operations.

Everything else is an error.

## Error Strategy
- Errors are raised during graph construction (not runtime).
- Format:

```
[EasyWork AST] Variable 'y' may be undefined after if-block at line 23
```

## Unsupported (Initial)
- `break`, `continue`, `return` in transformed region.
- Complex control-flow beyond `if/elif/else`.

## Tests
- If/else both assign same variable -> OK.
- Only one branch defines variable -> use after if -> error.
- Nested ifs with mixed definitions.
- `construct()` with non-graph statement -> error.
- `@ew.ast_transform` on normal function -> works.
- Non-graph `if` condition -> not transformed.
