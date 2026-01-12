"""
完整的类型系统测试
测试类型化节点、类型检查、Python 端集成
"""

import sys
import os
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))

import easywork as ew


# ========== 测试 1：基本类型化节点 ==========

def test_basic_typed_nodes():
    """测试基本的 int 类型节点"""
    class IntPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.source = ew.module.NumberSource(start=0, max=5, step=1)
            self.multiplier = ew.module.MultiplyBy(factor=3)
            self.text = ew.module.IntToText()
            self.prefix = ew.module.PrefixText(prefix="[Value] ")

        def construct(self):
            x = self.source.read()
            y = self.multiplier(x)
            z = self.text(y)
            self.prefix(z)

    pipeline = IntPipeline()

    source_type = pipeline.source.raw.type_info
    assert source_type.methods[ew._core.ID_FORWARD].output_type.name == "int"

    mult_type = pipeline.multiplier.raw.type_info
    assert [t.name for t in mult_type.methods[ew._core.ID_FORWARD].input_types] == ["int"]
    assert mult_type.methods[ew._core.ID_FORWARD].output_type.name == "int"

    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()


# ========== 测试 2：类型错误检测 ==========

def test_type_error_detection():
    """测试类型错误是否能被正确检测"""
    class TypeCheckPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.counter = ew.module.NumberSource(start=0, max=3, step=1)
            self.prefix = ew.module.PrefixText()

        def construct(self):
            x = self.counter.read()
            self.prefix(x)

    with pytest.raises(TypeError):
        TypeCheckPipeline().validate()


# ========== 测试 3：节点类型信息查询 ==========

def test_node_type_info():
    """测试节点类型信息查询功能"""
    registry = ew._core._NodeRegistry.instance()
    nodes = registry.registered_nodes()

    assert "NumberSource" in nodes
    assert "MultiplyBy" in nodes
    assert "PrefixText" in nodes

    counter = ew.module.NumberSource(0, 10, 1)
    type_info = counter.raw.type_info
    assert [t.name for t in type_info.methods[ew._core.ID_FORWARD].input_types] == []
    assert type_info.methods[ew._core.ID_FORWARD].output_type.name == "int"


# ========== 测试 4：Symbol 和连接 ==========

def test_symbol_connections():
    """测试 Symbol 和节点连接机制"""
    counter = ew.module.NumberSource(0, 5, 1)
    multiplier = ew.module.MultiplyBy(2)

    symbol = ew.Symbol(counter.raw)
    assert symbol.producer_node is counter.raw
    assert symbol.tuple_index is None

    assert isinstance(counter, ew.NodeWrapper)
    assert counter.built is False
    assert isinstance(multiplier, ew.NodeWrapper)


# ========== 测试 5：模块动态访问 ==========

def test_module_dynamic_access():
    """测试动态模块访问"""
    available = dir(ew.module)
    assert "NumberSource" in available

    with pytest.raises(AttributeError):
        _ = ew.module.NonExistentNode


# ========== 测试 6：Tuple 自动索引与多输入 ==========

def test_tuple_auto_index_and_multi_input():
    """测试 tuple 自动解包与多输入节点连接"""
    class TuplePipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.emitter = ew.module.PairEmitter(start=1, max=3)
            self.multiplier = ew.module.MultiplyBy(factor=2)
            self.joiner = ew.module.PairJoiner()
            self.prefix = ew.module.PrefixText()

        def construct(self):
            number, text = self.emitter.read()
            doubled = self.multiplier(number)
            joined = self.joiner(doubled, text)
            self.prefix(joined)

    pipeline = TuplePipeline()
    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()


# ========== 测试 7：Small Buffer 析构安全 ==========

def test_small_buffer_safety():
    """测试 SBO 析构是否正确释放小类型"""
    class SmallTrackedPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.source = ew.module.SmallTrackedSource(max=3)
            self.consumer = ew.module.SmallTrackedConsumer()

        def construct(self):
            value = self.source.read()
            self.consumer(value)

    ew._core.reset_small_tracked_live_count()
    pipeline = SmallTrackedPipeline()
    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()

    del pipeline
    import gc
    gc.collect()

    live_count = ew._core.get_small_tracked_live_count()
    assert live_count == 0


# ========== 测试 8：方法分发与 set_input_for ==========

def test_method_dispatch():
    """测试 method 分发能区分不同的上游连接"""
    class MethodDispatchPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.source = ew.module.NumberSource(start=0, max=2, step=1)
            self.recorder = ew.module.MethodDispatchRecorder()

        def construct(self):
            value = self.source.read()
            self.recorder.left(value)
            self.recorder.right(value)
            self.recorder(value)

    ew._core.reset_method_dispatch_counts()
    pipeline = MethodDispatchPipeline()
    pipeline.validate()
    pipeline.open()
    pipeline.run()
    pipeline.close()

    left_count, right_count, forward_count = ew._core.get_method_dispatch_counts()
    order_errors = ew._core.get_method_dispatch_order_errors()

    assert left_count == 3
    assert right_count == 3
    assert forward_count == 3
    assert order_errors == 0


# ========== 测试 9：Open/Close 参数与运行检查 ==========

def test_open_required_and_args():
    class OpenPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.source = ew.module.NumberSource(start=0, max=1, step=1)
            self.consumer = ew.module.MultiplyBy(factor=1)

        def construct(self):
            self.consumer(self.source.read())

    pipeline = OpenPipeline()
    pipeline.validate()

    with pytest.raises(RuntimeError):
        pipeline.run()

    pipeline.source.open("model", "cpu")
    pipeline.consumer.open()
    pipeline.run()
    pipeline.close()
