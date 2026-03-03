import easywork as ew


class BoomNode(ew.PythonNode):
    __ew_methods__ = ("forward",)

    def forward(self, value):
        raise RuntimeError("boom")


def test_error_code_default_ok():
    graph = ew._core.ExecutionGraph()
    assert graph.last_error_code() == ew._core.ErrorCode.Ok
    assert graph.last_error_code_name() == "EW_OK"


def test_python_dispatch_error_code():
    pipeline = ew.Pipeline()
    src = ew.module.NumberSource(start=0, max=0, step=1)
    boom = BoomNode()
    sink = ew.module.IntToText()

    with pipeline:
        value = src.read()
        value = boom(value)
        sink(value)

    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()

    assert pipeline._graph.error_count() >= 1
    assert pipeline._graph.last_error_code() == ew._core.ErrorCode.PythonDispatchError
    assert pipeline._graph.last_error_code_name() == "EW_PY_DISPATCH_ERROR"

