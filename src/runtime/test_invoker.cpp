#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include "core.h"
#include "macros.h"

using namespace easywork;

class TestNode : public BaseNode<TestNode> {
public:
    int add(int a, int b) {
        return a + b;
    }

    void set_val(int v) {
        val_ = v;
    }
    
    int get_val() {
        return val_;
    }

    EW_ENABLE_METHODS(add, set_val, get_val)

private:
    int val_{0};
};

int main() {
    TestNode node;
    
    // Test 1: Add (int, int) -> int
    std::cout << "Testing Add..." << std::endl;
    auto invoker_add = CreateInvoker(&TestNode::add);
    std::vector<Packet> inputs_add;
    inputs_add.push_back(Packet::From(10, 0));
    inputs_add.push_back(Packet::From(20, 0));
    
    Packet result_add = invoker_add(&node, inputs_add);
    assert(result_add.has_value());
    assert(result_add.cast<int>() == 30);
    std::cout << "  Passed" << std::endl;

    // Test 2: Set (int) -> void
    std::cout << "Testing Set..." << std::endl;
    auto invoker_set = CreateInvoker(&TestNode::set_val);
    std::vector<Packet> inputs_set;
    inputs_set.push_back(Packet::From(100, 0));
    
    Packet result_set = invoker_set(&node, inputs_set);
    assert(!result_set.has_value()); // Void returns empty packet
    assert(node.get_val() == 100);
    std::cout << "  Passed" << std::endl;
    
    // Test 3: Get () -> int
    std::cout << "Testing Get..." << std::endl;
    auto invoker_get = CreateInvoker(&TestNode::get_val);
    std::vector<Packet> inputs_get; // Empty
    
    Packet result_get = invoker_get(&node, inputs_get);
    assert(result_get.has_value());
    assert(result_get.cast<int>() == 100);
    std::cout << "  Passed" << std::endl;

    // Test 4: Type Mismatch
    std::cout << "Testing Type Mismatch..." << std::endl;
    std::vector<Packet> inputs_bad;
    inputs_bad.push_back(Packet::From(std::string("wrong"), 0));
    inputs_bad.push_back(Packet::From(20, 0));
    
    try {
        invoker_add(&node, inputs_bad);
        assert(false && "Should have thrown type mismatch");
    } catch (const std::exception& e) {
        std::cout << "  Caught expected error: " << e.what() << std::endl;
    }
    
    std::cout << "All C++ Invoker Tests Passed!" << std::endl;
    return 0;
}
