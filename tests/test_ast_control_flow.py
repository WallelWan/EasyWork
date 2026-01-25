import pytest
import easywork as ew


def test_if_branches_define_value():
    class BranchPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            cond = ew.module.EvenCheck()(source())
            left = ew.module.MultiplyBy(2)
            right = ew.module.MultiplyBy(3)
            if cond:
                y = left(source())
            else:
                y = right(source())
            text = ew.module.IntToText()
            text(y)

    pipeline = BranchPipeline()
    pipeline.validate()


def test_if_condition_call_chain():
    class CallChainPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            if ew.module.EvenCheck()(source()):
                y = source()
            else:
                y = source()
            ew.module.IntToText()(y)

    pipeline = CallChainPipeline()
    pipeline.validate()


def test_if_condition_int_allowed():
    class IntCondPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            if source():
                y = ew.module.MultiplyBy(2)(source())
            else:
                y = ew.module.MultiplyBy(3)(source())
            ew.module.IntToText()(y)

    pipeline = IntCondPipeline()
    pipeline.validate()


def test_runtime_branch_execution_counts():
    class BranchRuntimePipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 3, 1)
            cond = ew.module.EvenCheck()(source())
            recorder = ew.module.MethodDispatchRecorder()
            if cond:
                y = recorder.left(source())
            else:
                y = recorder.right(source())
            ew.module.IntToText()(y)

    ew._core.reset_method_dispatch_counts()
    pipeline = BranchRuntimePipeline()
    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()
    left_count, right_count, forward_count = ew._core.get_method_dispatch_counts()
    assert left_count + right_count == 4
    assert left_count == 2
    assert right_count == 2
    assert forward_count == 0


def test_nested_if_all_paths_define():
    class NestedPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            cond = ew.module.EvenCheck()(source())
            if cond:
                if cond:
                    y = ew.module.MultiplyBy(2)(source())
                else:
                    y = ew.module.MultiplyBy(3)(source())
            else:
                y = ew.module.MultiplyBy(4)(source())
            ew.module.IntToText()(y)

    pipeline = NestedPipeline()
    pipeline.validate()


def test_if_missing_branch_definition_raises():
    class MissingBranchPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            cond = ew.module.EvenCheck()(source())
            node = ew.module.MultiplyBy(2)
            if cond:
                y = node(source())
            text = ew.module.IntToText()
            text(y)

    pipeline = MissingBranchPipeline()
    with pytest.raises(SyntaxError):
        pipeline.validate()


def test_non_graph_assignment_raises():
    class NonGraphPipeline(ew.Pipeline):
        def construct(self):
            value = 1
            source = ew.module.NumberSource(0, 1, 1)
            source(value)

    pipeline = NonGraphPipeline()
    with pytest.raises(SyntaxError):
        pipeline.validate()


def test_ast_transform_decorator():
    def build_graph():
        source = ew.module.NumberSource(0, 1, 1)
        cond = ew.module.EvenCheck()(source())
        if cond:
            y = source()
        else:
            y = source()
        ew.module.IntToText()(y)

    transformed = ew.ast_transform(build_graph)
    assert getattr(transformed, "__ew_ast_transformed__", False) is True


def test_non_graph_condition_raises():
    class BadConditionPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            if True:
                y = source()
            else:
                y = source()
            ew.module.IntToText()(y)

    pipeline = BadConditionPipeline()
    with pytest.raises(SyntaxError):
        pipeline.validate()


def test_if_condition_non_bool_raises():
    class BadTypedCondPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            cond = ew.module.IntToText()(source())
            if cond:
                y = source()
            else:
                y = source()
            ew.module.IntToText()(y)

    pipeline = BadTypedCondPipeline()
    with pytest.raises(TypeError):
        pipeline.validate()


def test_if_condition_raises_on_construct():
    class BadTypedCondPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            cond = ew.module.IntToText()(source())
            if cond:
                y = source()
            else:
                y = source()
            ew.module.IntToText()(y)

    pipeline = BadTypedCondPipeline()
    with pytest.raises(TypeError):
        pipeline._build_topology()


def test_branch_type_mismatch_runtime_error():
    class MismatchPipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            cond = ew.module.EvenCheck()(source())
            if cond:
                y = ew.module.IntToText()(source())
            else:
                y = ew.module.MultiplyBy(2)(source())
            ew.module.PrefixText()(y)

    pipeline = MismatchPipeline()
    with pytest.raises(TypeError):
        pipeline.validate()

def test_implicit_branch_update():
    class UpdatePipeline(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            x = source()
            cond = ew.module.EvenCheck()(x)
            
            if cond:
                x = ew.module.MultiplyBy(2)(x)
            
            ew.module.IntToText()(x)

    pipeline = UpdatePipeline()
    pipeline.validate()


def test_implicit_branch_update_false_branch():
    class UpdatePipelineFalse(ew.Pipeline):
        def construct(self):
            source = ew.module.NumberSource(0, 1, 1)
            x = source()
            cond = ew.module.EvenCheck()(x)
            
            if cond:
                pass
            else:
                x = ew.module.MultiplyBy(2)(x)
            
            ew.module.IntToText()(x)

    pipeline = UpdatePipelineFalse()
    pipeline.validate()
