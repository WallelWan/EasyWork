"""
测试 Eager Mode (即时执行) 与 Tracing Mode (with 语法构图) 的混合使用
"""

import pytest
import easywork as ew

# ========== 测试 1：Eager Mode 直接调用 ==========

def test_eager_execution():
    """测试在没有 Pipeline 上下文时直接调用节点方法"""
    multiplier = ew.module.MultiplyBy(factor=5)
    text_converter = ew.module.IntToText()
    
    result_int = multiplier(10) # 10 * 5 = 50
    assert isinstance(result_int, int)
    assert result_int == 50
    
    result_str = text_converter(multiplier(2)) # 2 * 5 = 10 -> "10"
    assert isinstance(result_str, str)
    assert result_str == "10"
    
    mixed = ew.module.MixedNode()
    
    res1 = mixed(5) 
    assert res1 == 5 # 默认 length_ 为 0
    
    ret = mixed.set_string("hello")
    assert ret is None
    
    res2 = mixed(5) # 5 + len("hello") = 10
    assert res2 == 10
    
    res3 = mixed.compute_ratio(10, 2)
    assert isinstance(res3, float)
    assert res3 == 5.0


# ========== 测试 2：Tracing Mode (with 语法) ==========

def test_tracing_with_context():
    """测试使用 with pipeline: 语法进行显式构图"""
    pipeline = ew.Pipeline()
    
    src = ew.module.NumberSource(start=1, max=1, step=1)
    proc = ew.module.MultiplyBy(factor=2)
    sink = ew.module.IntToText() # Use compatible sink (int -> string)
    
    print("Verifying Eager Execution...")
    assert proc(100) == 200
    
    print("Entering Tracing Mode...")
    with pipeline:
        data = src.read()
        assert isinstance(data, ew.Symbol)
        
        result = proc(data)
        assert isinstance(result, ew.Symbol)
        
        sink(result)
        
    print("Running Pipeline...")
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
            src = ew.module.NumberSource(0, 0, 1)
            src.open()
            sym = self.multiplier(src.read())
            assert isinstance(sym, ew.Symbol)
    
    pipeline = HybridPipeline()
    
    assert pipeline.multiplier(5) == 50
    
    pipeline.open()
    pipeline.run()
    pipeline.close()

# ========== 测试 4：错误处理 ==========

def test_eager_bad_args():
    """测试 Eager 模式下的参数错误"""
    multiplier = ew.module.MultiplyBy(factor=2)
    
    with pytest.raises(RuntimeError) as exc:
        multiplier("not an int")
    assert "No conversion handler for target type" in str(exc.value)
    
    with pytest.raises(RuntimeError) as exc:
        multiplier(1, 2) # forward 只接受 1 个参数
    assert "argument count mismatch" in str(exc.value)


def test_number_source_invalid_step_eager():
    with pytest.raises(RuntimeError):
        _ = ew.module.NumberSource(start=0, max=1, step=0)
