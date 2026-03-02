import easywork as ew


class FailableNode(ew.PythonNode):
    __ew_methods__ = ("forward",)

    def __init__(self, fail_on):
        self._fail_on = fail_on

    def forward(self, value):
        if value == self._fail_on:
            raise RuntimeError("boom")
        return value


class CollectorNode(ew.PythonNode):
    __ew_methods__ = ("forward",)

    def __init__(self, out):
        self._out = out

    def forward(self, value):
        self._out.append(value)
        return value


def _run_pipeline(policy=None):
    pipeline = ew.Pipeline()
    if policy is not None:
        pipeline.set_error_policy(policy)

    out = []
    src = ew.module.NumberSource(start=0, max=3, step=1)
    fail = FailableNode(1)
    sink = CollectorNode(out)

    with pipeline:
        value = src.read()
        value = fail(value)
        sink(value)

    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()

    return pipeline, out


def test_error_policy_default_failfast():
    pipeline, out = _run_pipeline()
    assert out == [0]
    assert pipeline._graph.error_count() >= 1
    assert "Dispatch Error" in pipeline._graph.last_error()


def test_error_policy_skip_current_data():
    pipeline, out = _run_pipeline("skip")
    assert out == [0, 2, 3]
    assert pipeline.get_error_policy() == ew._core.ErrorPolicy.SkipCurrentData
