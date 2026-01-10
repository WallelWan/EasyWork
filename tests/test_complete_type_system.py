#!/usr/bin/env python3
"""
完整的类型系统测试
测试类型化节点、类型检查、Python 端集成
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

import easywork as ew

# ========== 测试 1：基本类型化节点 ==========
def test_basic_typed_nodes():
    """测试基本的 int 类型节点"""
    print("\n=== 测试 1：基本类型化节点 ===")

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

    try:
        pipeline = IntPipeline()
        print("✓ 成功创建类型化节点")

        # 测试类型信息
        source_type = pipeline.source.raw.type_info
        print(f"  NumberSource output_types: {[t.name for t in source_type.output_types]}")

        mult_type = pipeline.multiplier.raw.type_info
        print(f"  MultiplyBy input_types: {[t.name for t in mult_type.input_types]}")
        print(f"  MultiplyBy output_types: {[t.name for t in mult_type.output_types]}")

        # 验证类型检查
        pipeline.validate()
        print("✓ 类型检查通过")

        # 运行
        pipeline.run()
        print("✓ Pipeline 运行成功")

        return True
    except Exception as e:
        print(f"✗ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False


# ========== 测试 2：类型错误检测 ==========
def test_type_error_detection():
    """测试类型错误是否能被正确检测"""
    print("\n=== 测试 2：类型错误检测 ===")

    # 注意：当前实现中 PrefixText 接收 std::string
    # NumberSource 输出 int
    # 如果直接连接，在 C++ 端会进行类型转换
    # 这个测试演示类型系统的基本功能

    class TypeCheckPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.counter = ew.module.NumberSource(start=0, max=3, step=1)
            self.prefix = ew.module.PrefixText()

        def construct(self):
            x = self.counter.read()  # int
            # PrefixText 期望 std::string，但会收到 int
            # 在 C++ 端这会导致 Value 转换错误
            self.prefix(x)

    try:
        pipeline = TypeCheckPipeline()
        pipeline.validate()
        print("✗ 应该在类型检查阶段抛出 TypeError")
        return False
    except TypeError as e:
        print(f"✓ 正确检测到类型错误: {e}")
        return True
    except Exception as e:
        print(f"✗ 其他错误: {e}")
        return False


# ========== 测试 3：节点类型信息查询 ==========
def test_node_type_info():
    """测试节点类型信息查询功能"""
    print("\n=== 测试 3：节点类型信息查询 ===")

    try:
        # 查询所有已注册节点
        registry = ew._core._NodeRegistry.instance()
        nodes = registry.registered_nodes()
        print(f"✓ 已注册的节点: {nodes}")

        # 检查新节点是否注册
        assert "NumberSource" in nodes
        assert "MultiplyBy" in nodes
        assert "PrefixText" in nodes
        print("✓ 新节点都已正确注册")

        # 创建节点并查询类型信息
        counter = ew.module.NumberSource(0, 10, 1)
        type_info = counter.raw.type_info
        print(f"  NumberSource 类型信息:")
        print(f"    输入类型: {[t.name for t in type_info.input_types]}")
        print(f"    输出类型: {[t.name for t in type_info.output_types]}")

        return True
    except Exception as e:
        print(f"✗ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False


# ========== 测试 4：Symbol 和连接 ==========
def test_symbol_connections():
    """测试 Symbol 和节点连接机制"""
    print("\n=== 测试 4：Symbol 和连接机制 ===")

    try:
        # 创建节点
        counter = ew.module.NumberSource(0, 5, 1)
        multiplier = ew.module.MultiplyBy(2)

        # 创建 Symbol
        symbol = ew.Symbol(counter.raw)
        print(f"✓ 创建 Symbol: producer={type(symbol.producer_node).__name__}")
        print(f"  tuple_index={symbol.tuple_index}")

        # 测试节点包装器
        assert isinstance(counter, ew.NodeWrapper)
        assert counter.built == False
        print("✓ NodeWrapper 工作正常")

        return True
    except Exception as e:
        print(f"✗ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False


# ========== 测试 5：模块动态访问 ==========
def test_module_dynamic_access():
    """测试动态模块访问"""
    print("\n=== 测试 5：模块动态访问 ===")

    try:
        # 测试 __dir__ 方法
        available = dir(ew.module)
        print(f"✓ 可用节点: {available}")

        # 测试访问不存在的节点
        try:
            invalid_node = ew.module.NonExistentNode
            print("✗ 应该抛出 AttributeError")
            return False
        except AttributeError as e:
            print(f"✓ 正确抛出 AttributeError: {e}")

        return True
    except Exception as e:
        print(f"✗ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False


# ========== 测试 6：Tuple 自动索引与多输入 ==========
def test_tuple_auto_index_and_multi_input():
    """测试 tuple 自动解包与多输入节点连接"""
    print("\n=== 测试 6：Tuple 自动索引与多输入 ===")

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

    try:
        pipeline = TuplePipeline()
        pipeline.validate()
        pipeline.run()
        print("✓ Tuple 解包与多输入执行成功")
        return True
    except Exception as e:
        print(f"✗ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False


# ========== 测试 7：Small Buffer 析构安全 ==========
def test_small_buffer_safety():
    """测试 SBO 析构是否正确释放小类型"""
    print("\n=== 测试 7：Small Buffer 析构安全 ===")

    class SmallTrackedPipeline(ew.Pipeline):
        def __init__(self):
            super().__init__()
            self.source = ew.module.SmallTrackedSource(max=3)
            self.consumer = ew.module.SmallTrackedConsumer()

        def construct(self):
            value = self.source.read()
            self.consumer(value)

    try:
        ew._core.reset_small_tracked_live_count()
        pipeline = SmallTrackedPipeline()
        pipeline.validate()
        pipeline.run()
        live_count = ew._core.get_small_tracked_live_count()
        if live_count != 0:
            print(f"✗ SmallTracked 实例未清理，live_count={live_count}")
            return False
        print("✓ SmallTracked 实例已正确析构")
        return True
    except Exception as e:
        print(f"✗ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False


# ========== 主测试函数 ==========
def main():
    print("=" * 60)
    print("EasyWork 完整类型系统测试")
    print("=" * 60)

    results = []

    # 运行所有测试
    results.append(("基本类型化节点", test_basic_typed_nodes()))
    results.append(("类型错误检测", test_type_error_detection()))
    results.append(("节点类型信息查询", test_node_type_info()))
    results.append(("Symbol 和连接", test_symbol_connections()))
    results.append(("模块动态访问", test_module_dynamic_access()))
    results.append(("Tuple 自动索引与多输入", test_tuple_auto_index_and_multi_input()))
    results.append(("Small Buffer 析构安全", test_small_buffer_safety()))

    # 汇总结果
    print("\n" + "=" * 60)
    print("测试结果汇总")
    print("=" * 60)

    passed = 0
    failed = 0

    for name, result in results:
        status = "✓ 通过" if result else "✗ 失败"
        print(f"{name}: {status}")
        if result:
            passed += 1
        else:
            failed += 1

    print(f"\n总计: {passed} 通过, {failed} 失败")

    if failed == 0:
        print("\n🎉 所有测试通过！类型系统实现成功！")
        return 0
    else:
        print(f"\n⚠ 有 {failed} 个测试失败")
        return 1


if __name__ == "__main__":
    sys.exit(main())
