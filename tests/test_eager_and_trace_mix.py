"""
测试 Eager Mode (即时执行) 与 Tracing Mode (with 语法构图) 的混合使用
"""

import sys
import os
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))

import easywork as ew

# ========== 测试 1：Eager Mode 直接调用 ==========

def test_eager_execution():
    """测试在没有 Pipeline 上下文时直接调用节点方法"""
    # 1. 初始化节点
    multiplier = ew.module.MultiplyBy(factor=5)
    text_converter = ew.module.IntToText()
    
    # 2. 直接调用 (应该返回 int)
    result_int = multiplier(10) # 10 * 5 = 50
    assert isinstance(result_int, int)
    assert result_int == 50
    
    # 3. 链式直接调用 (应该返回 string)
    result_str = text_converter(multiplier(2)) # 2 * 5 = 10 -> "10"
    assert isinstance(result_str, str)
    assert result_str == "10"
    
    # 4. 测试异构方法调用
    mixed = ew.module.MixedNode()
    
    # forward: int -> int
    res1 = mixed(5) 
    assert res1 == 5 # 默认 length_ 为 0
    
    # set_string: string -> void
    # 注意：调用返回 void 的方法，bindings 可能会返回 None
    ret = mixed.set_string("hello")
    assert ret is None
    
    # 状态应该已更新，再次调用 forward
    res2 = mixed(5) # 5 + len("hello") = 10
    assert res2 == 10
    
    # compute_ratio: int, int -> double
    res3 = mixed.compute_ratio(10, 2)
    assert isinstance(res3, float)
    assert res3 == 5.0


# ========== 测试 2：Tracing Mode (with 语法) ==========

def test_tracing_with_context():
    """测试使用 with pipeline: 语法进行显式构图"""
    pipeline = ew.Pipeline()
    
    # 初始化节点 (可以在 with 外面)
    src = ew.module.NumberSource(start=1, max=1, step=1)
    proc = ew.module.MultiplyBy(factor=2)
    sink = ew.module.IntToText() # Use compatible sink (int -> string)
    
    print("Verifying Eager Execution...")
    # 在 Eager 环境下验证节点功能 (不影响后续构图，因为是无状态的或者状态独立的)
    assert proc(100) == 200
    
    print("Entering Tracing Mode...")
    # 进入 Tracing 模式
    with pipeline:
        data = src.read()
        # 这里返回的应该是 Symbol
        assert isinstance(data, ew.Symbol)
        
        result = proc(data)
        assert isinstance(result, ew.Symbol)
        
        sink(result)
        
    print("Running Pipeline...")
    # 验证并运行
    pipeline.validate()
    pipeline.open()
    pipeline.run() # 应该处理 1 个数据: 1 * 2 = 2
    pipeline.close()
    print("Pipeline Finished.")


# ========== 测试 3：混合模式与 construct 方法 ==========

def test_construct_vs_with():
    """验证 construct 方法内默认是 Tracing 模式，不受外部影响"""
    
    class HybridPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.multiplier = ew.module.MultiplyBy(factor=10)
            
        def construct(self):
            # 这里必须是 Tracing 模式
            # 即使我们创建一个临时的 Source
            src = ew.module.NumberSource(0, 0, 1)
            # Temp nodes created in construct must be opened explicitly 
            # if they missed the pipeline.open() call
            src.open()
            sym = self.multiplier(src.read())
            assert isinstance(sym, ew.Symbol)
    
    pipeline = HybridPipeline()
    
    # 在外部调用是 Eager
    assert pipeline.multiplier(5) == 50
    
    pipeline.open()
    # 运行 Pipeline (触发 construct)
    pipeline.run()
    pipeline.close()

# ========== 测试 4：错误处理 ==========

def test_eager_bad_args():
    """测试 Eager 模式下的参数错误"""
    multiplier = ew.module.MultiplyBy(factor=2)
    
    # 参数类型错误 (C++ 抛出异常 -> Python RuntimeError)
    with pytest.raises(RuntimeError) as exc:
        multiplier("not an int")
    assert "Failed to convert argument" in str(exc.value)
    
    # 参数数量错误
    with pytest.raises(RuntimeError) as exc:
        multiplier(1, 2) # forward 只接受 1 个参数
    assert "argument count mismatch" in str(exc.value)

