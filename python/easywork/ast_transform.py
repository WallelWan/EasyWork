import inspect
import textwrap
import types

import gast


class AstTransformError(SyntaxError):
    pass


_DEFAULT_IF_SYMBOL = None


def set_default_if_symbol(if_symbol):
    global _DEFAULT_IF_SYMBOL
    _DEFAULT_IF_SYMBOL = if_symbol


def ast_transform(fn=None, *, strict=True):
    def decorator(func):
        return transform_function(func, strict=strict, if_symbol=_DEFAULT_IF_SYMBOL)

    if fn is None:
        return decorator
    return decorator(fn)


def transform_function(fn, *, strict=True, if_symbol=None):
    source = inspect.getsource(fn)
    source = textwrap.dedent(source)
    module_ast = gast.parse(source)
    transformer = _GraphAstTransformer(target_name=fn.__name__, strict=strict)
    module_ast = transformer.visit(module_ast)
    gast.fix_missing_locations(module_ast)

    if transformer.errors:
        raise AstTransformError("\n".join(transformer.errors))

    py_ast = gast.gast_to_ast(module_ast)
    code = compile(py_ast, filename=inspect.getsourcefile(fn) or "<ast>", mode="exec")
    namespace = dict(fn.__globals__)
    if if_symbol is None:
        if_symbol = _DEFAULT_IF_SYMBOL
    if if_symbol is not None:
        namespace.setdefault("if_", if_symbol)
    exec(code, namespace)
    new_fn = namespace[fn.__name__]
    new_fn.__ew_ast_transformed__ = True
    return new_fn


class _GraphAstTransformer(gast.NodeTransformer):
    def __init__(self, *, target_name, strict=True):
        self.target_name = target_name
        self.strict = strict
        self.errors = []
        self._in_target = False
        self._defined = set()
        self._maybe_undef = set()
        self._branch_stack = []
        self._if_counter = 0

    def visit_FunctionDef(self, node):
        if node.name != self.target_name:
            if self._in_target:
                self._error(node, "Nested function definitions are not allowed in graph construction")
            return node

        previous_state = (self._in_target, self._defined, self._maybe_undef)
        self._in_target = True
        self._defined = set()
        self._maybe_undef = set()
        node.body = self._visit_statements(node.body)
        self._in_target = False
        self._defined, self._maybe_undef = previous_state[1], previous_state[2]
        return node

    def visit_ClassDef(self, node):
        if self._in_target:
            self._error(node, "Class definitions are not allowed in graph construction")
        return node

    def visit_Return(self, node):
        if self._in_target:
            self._error(node, "'return' is not supported in graph construction")
        return node

    def visit_For(self, node):
        if self._in_target:
            self._error(node, "'for' is not supported in graph construction")
        return node

    def visit_While(self, node):
        if self._in_target:
            self._error(node, "'while' is not supported in graph construction")
        return node

    def visit_Try(self, node):
        if self._in_target:
            self._error(node, "'try' is not supported in graph construction")
        return node

    def visit_With(self, node):
        if self._in_target:
            self._error(node, "'with' is not supported in graph construction")
        return node

    def visit_Expr(self, node):
        if not self._in_target:
            return node
        if not self._is_graph_expr(node.value):
            self._error(node, "Only graph expressions are allowed in graph construction")
        return self.generic_visit(node)

    def visit_Assign(self, node):
        if not self._in_target:
            return node
        if not self._is_graph_expr(node.value):
            self._error(node, "Only graph assignments are allowed in graph construction")
            return node

        target_names = self._extract_target_names(node.targets)
        for name in target_names:
            self._defined.add(name)
            self._maybe_undef.discard(name)
            if getattr(self, "_assigned", None) is not None:
                self._assigned.add(name)

        assign_node = self.generic_visit(node)
        if self._branch_stack and target_names:
            br_name = self._branch_stack[-1]
            extra_nodes = [assign_node]
            for name in target_names:
                extra_nodes.append(self._make_br_assign(br_name, name))
            return extra_nodes
        return assign_node

    def visit_AugAssign(self, node):
        if self._in_target:
            self._error(node, "Augmented assignments are not supported in graph construction")
        return node

    def visit_Name(self, node):
        if self._in_target and isinstance(node.ctx, gast.Load):
            if node.id in self._maybe_undef:
                self._error(node, f"Variable '{node.id}' may be undefined after if-block")
        return node

    def visit_If(self, node):
        if not self._in_target:
            return node
        if not self._is_graph_condition(node.test):
            self._error(node, "If condition must be a graph expression in construct()")
            return node

        pre_defined = set(self._defined)
        pre_maybe = set(self._maybe_undef)
        br_name = f"__ew_br_{self._if_counter}"
        self._if_counter += 1

        true_defined, true_maybe, true_assigned, true_body = self._visit_branch(
            node.body, pre_defined, pre_maybe, br_name
        )
        false_defined, false_maybe, false_assigned, false_body = self._visit_branch(
            node.orelse or [], pre_defined, pre_maybe, br_name
        )

        assigned_out = true_assigned & false_assigned
        
        # New Logic: Implicit Updates
        implicit_update_candidates = (true_assigned ^ false_assigned) & pre_defined
        
        implied_true = implicit_update_candidates - true_assigned
        for name in implied_true:
             if true_body is None: true_body = []
             true_body.append(self._make_br_assign(br_name, name))
             
        implied_false = implicit_update_candidates - false_assigned
        for name in implied_false:
             if false_body is None: false_body = []
             false_body.append(self._make_br_assign(br_name, name))
             
        assigned_out = assigned_out | implicit_update_candidates

        maybe_undef = (true_assigned | false_assigned) - assigned_out

        self._defined = pre_defined | assigned_out
        self._maybe_undef = (pre_maybe | true_maybe | false_maybe | maybe_undef) - assigned_out
        if getattr(self, "_assigned", None) is not None:
            self._assigned.update(assigned_out)

        true_with = self._wrap_branch(true_body or [gast.Pass()], br_name, "true")
        false_with = self._wrap_branch(false_body or [gast.Pass()], br_name, "false")

        with_node = gast.With(
            items=[gast.withitem(context_expr=self._make_if_call(node.test), optional_vars=gast.Name(id=br_name, ctx=gast.Store()))],
            body=[true_with, false_with],
        )

        post_nodes = [with_node]
        for name in sorted(assigned_out):
            post_nodes.append(self._make_br_value_assign(br_name, name))
            if self._branch_stack:
                parent_br = self._branch_stack[-1]
                post_nodes.append(self._make_br_assign(parent_br, name))

        return post_nodes

    def _visit_statements(self, statements):
        new_body = []
        for stmt in statements:
            result = self.visit(stmt)
            if isinstance(result, list):
                new_body.extend(result)
            elif result is not None:
                new_body.append(result)
        return new_body

    def _visit_branch(self, statements, defined, maybe_undef, br_name):
        prev_defined = self._defined
        prev_maybe = self._maybe_undef

        self._defined = set(defined)
        self._maybe_undef = set(maybe_undef)
        assigned = set()

        prev_assigned = getattr(self, "_assigned", None)
        prev_stack = list(self._branch_stack)
        self._assigned = assigned
        self._branch_stack.append(br_name)

        body = self._visit_statements(statements)
        defined_out = set(self._defined)
        maybe_out = set(self._maybe_undef)

        self._defined = prev_defined
        self._maybe_undef = prev_maybe
        self._assigned = prev_assigned
        self._branch_stack = prev_stack

        return defined_out, maybe_out, assigned, body

    def _extract_target_names(self, targets):
        names = []
        for target in targets:
            if isinstance(target, gast.Name):
                names.append(target.id)
            elif isinstance(target, gast.Tuple):
                for elt in target.elts:
                    if isinstance(elt, gast.Name):
                        names.append(elt.id)
                    else:
                        self._error(target, "Tuple assignment targets must be simple names")
            else:
                self._error(target, "Assignment targets must be simple names")
        return names

    def _is_graph_condition(self, node):
        if isinstance(node, gast.Name):
            return node.id in self._defined
        if isinstance(node, gast.Call):
            return self._is_graph_expr(node, allow_literal=False)
        return False

    def _is_graph_expr(self, node, *, allow_literal=False):
        if isinstance(node, gast.Name):
            return node.id in self._defined
        if isinstance(node, gast.Constant):
            return allow_literal
        if isinstance(node, gast.Call):
            if isinstance(node.func, gast.Call):
                if not self._is_graph_expr(node.func, allow_literal=allow_literal):
                    return False
            elif not isinstance(node.func, (gast.Name, gast.Attribute)):
                return False
            for arg in node.args:
                if not self._is_graph_expr(arg, allow_literal=True):
                    return False
            for keyword in node.keywords:
                if not self._is_graph_expr(keyword.value, allow_literal=True):
                    return False
            return True
        if isinstance(node, gast.Tuple):
            return all(self._is_graph_expr(elt, allow_literal=allow_literal) for elt in node.elts)
        return False

    def _wrap_branch(self, body, br_name, branch_name):
        branch_attr = gast.Attribute(value=gast.Name(id=br_name, ctx=gast.Load()), attr=branch_name, ctx=gast.Load())
        return gast.With(items=[gast.withitem(context_expr=branch_attr, optional_vars=None)], body=body)

    def _make_if_call(self, cond):
        return gast.Call(func=gast.Name(id="if_", ctx=gast.Load()), args=[cond], keywords=[])

    def _make_br_assign(self, br_name, var_name):
        call = gast.Call(
            func=gast.Attribute(value=gast.Name(id=br_name, ctx=gast.Load()), attr="assign", ctx=gast.Load()),
            args=[gast.Constant(value=var_name), gast.Name(id=var_name, ctx=gast.Load())],
            keywords=[],
        )
        return gast.Expr(value=call)

    def _make_br_value_assign(self, br_name, var_name):
        call = gast.Call(
            func=gast.Attribute(value=gast.Name(id=br_name, ctx=gast.Load()), attr="value", ctx=gast.Load()),
            args=[gast.Constant(value=var_name)],
            keywords=[],
        )
        return gast.Assign(targets=[gast.Name(id=var_name, ctx=gast.Store())], value=call)

    def _error(self, node, message):
        lineno = getattr(node, "lineno", None)
        if lineno is not None:
            self.errors.append(f"[EasyWork AST] {message} at line {lineno}")
        else:
            self.errors.append(f"[EasyWork AST] {message}")
