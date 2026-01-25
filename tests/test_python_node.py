"""
测试 Python 节点跳板类与 C++ 对接
"""

import pytest
import easywork as ew


class PyAddOne(ew.PythonNode):
    def forward(self, x):
        return x + 1


class PyScale(ew.PythonNode):
    def forward(self, x, scale=2):
        return x * scale


def test_python_node_eager():
    node = PyAddOne()
    result = node(10)
    assert isinstance(result, int)
    assert result == 11


def test_python_node_pipeline():
    pipeline = ew.Pipeline()

    src = ew.module.NumberSource(start=1, max=1, step=1)
    py_node = PyAddOne()
    mul = ew.module.MultiplyBy(factor=2)
    sink = ew.module.IntToText()

    with pipeline:
        data = src.read()
        data = py_node(data)
        data = mul(data)
        sink(data)

    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()


def test_python_node_bad_arg_count():
    node = PyAddOne()
    with pytest.raises(TypeError): # Eager mode raises TypeError
         node(1, 2)
         
# Pipeline mode check
    pipeline = ew.Pipeline()
    node = PyAddOne()
    with pytest.raises(TypeError): # NodeWrapper raises TypeError
        with pipeline:
             node(1, 2)


def test_python_node_kwargs_eager():
    node = PyScale()
    result = node(3, scale=4)
    assert result == 12


def test_python_node_kwargs_pipeline():
    pipeline = ew.Pipeline()

    src = ew.module.NumberSource(start=1, max=1, step=1)
    py_node = PyScale()
    sink = ew.module.IntToText()

    with pipeline:
        data = src.read()
        data = py_node(data, scale=3)
        sink(data)

    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()


def test_python_node_defaults_pipeline():
    pipeline = ew.Pipeline()

    src = ew.module.NumberSource(start=1, max=1, step=1)
    py_node = PyScale()
    sink = ew.module.IntToText()

    with pipeline:
        data = src.read()
        data = py_node(data)
        sink(data)

    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()


def test_cpp_node_kwargs_pipeline_error():
    pipeline = ew.Pipeline()
    src = ew.module.NumberSource(start=1, max=1, step=1)
    mul = ew.module.MultiplyBy(factor=2)

    with pytest.raises(TypeError):
        with pipeline:
            data = src.read()
            mul(data, factor=3)
