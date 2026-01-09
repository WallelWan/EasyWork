#pragma once

#include <tbb/flow_graph.h>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include "memory/frame.h"

namespace easywork {

// The Graph Container
class ExecutionGraph {
public:
    tbb::flow::graph tbb_graph;
};

// Base class for all nodes
class Node {
public:
    virtual ~Node() = default;
    virtual void build(ExecutionGraph& g) = 0;
    
    // Phase 2 Fix: Deferred connection.
    // Store upstream nodes here, and connect in connect() phase.
    std::vector<Node*> upstreams_;
    
    void add_upstream(Node* n) {
        upstreams_.push_back(n);
    }
    
    // Must be called AFTER build()
    virtual void connect() = 0;
};

// ... (SourceNode, no inputs usually) ...
class SourceNode : public Node {
public:
    // ... (same as before)
    using OutputType = Frame;

    // Use OneTBB (2021+) input_node
    tbb::flow::input_node<Frame>* tbb_node = nullptr;

    void build(ExecutionGraph& g) override {
        // Use a local flag to track if we've stopped
        auto stopped = std::make_shared<std::atomic<bool>>(false);

        tbb_node = new tbb::flow::input_node<Frame>(g.tbb_graph,
            [this, stopped](tbb::flow_control& fc) -> Frame {
                // Check if already stopped
                if (stopped->exchange(true, std::memory_order_relaxed)) {
                    // Already stopped, don't call fc.stop() again
                    return nullptr;
                }

                // Try to generate a frame
                Frame f = this->generate();
                if (!f) {
                    // No more frames, signal stop
                    fc.stop();
                    return nullptr;
                }

                // Reset the flag for next iteration
                stopped->store(false, std::memory_order_relaxed);
                return f;
            }
        );
    }

    virtual Frame generate() = 0;

    void connect() override {
        // Source has no inputs
    }
};

// ... (ProcessNode) ...
class ProcessNode : public Node {
public:
    tbb::flow::function_node<Frame, Frame>* tbb_node = nullptr;

    void build(ExecutionGraph& g) override {
        tbb_node = new tbb::flow::function_node<Frame, Frame>(g.tbb_graph,
            tbb::flow::serial,
            [this](Frame f) -> Frame {
                if (!f) return nullptr;  // Skip null frames
                return this->process(f);
            }
        );
    }

    virtual Frame process(Frame input) = 0;

    void set_input(Node* upstream) {
        add_upstream(upstream);
    }

    void connect() override {
        for (auto* upstream : upstreams_) {
            if (auto src = dynamic_cast<SourceNode*>(upstream)) {
                tbb::flow::make_edge(*src->tbb_node, *tbb_node);
            } else if (auto proc = dynamic_cast<ProcessNode*>(upstream)) {
                tbb::flow::make_edge(*proc->tbb_node, *tbb_node);
            }
        }
    }
};

// ... (SinkNode) ...
class SinkNode : public Node {
public:
    tbb::flow::function_node<Frame, tbb::flow::continue_msg>* tbb_node = nullptr;

    void build(ExecutionGraph& g) override {
        tbb_node = new tbb::flow::function_node<Frame, tbb::flow::continue_msg>(g.tbb_graph,
            tbb::flow::serial,
            [this](Frame f) {
                if (f) {
                    this->consume(f);
                }
                return tbb::flow::continue_msg();
            }
        );
    }

    virtual void consume(Frame input) = 0;

    void set_input(Node* upstream) {
        add_upstream(upstream);
    }

    void connect() override {
        for (auto* upstream : upstreams_) {
            if (auto src = dynamic_cast<SourceNode*>(upstream)) {
                tbb::flow::make_edge(*src->tbb_node, *tbb_node);
            } else if (auto proc = dynamic_cast<ProcessNode*>(upstream)) {
                tbb::flow::make_edge(*proc->tbb_node, *tbb_node);
            }
        }
    }
};

// --- The Executor ---
class Executor {
public:
    void run(ExecutionGraph& g) {
        // TBB graph runs automatically once sources are active.
        // In OneTBB, input_node termination behavior differs from TBB 2020's source_node.
        // The graph may not properly terminate when input_node stops.
        // TODO: Implement proper node lifecycle management for OneTBB.
        // For now, we wait briefly to let frames flow through, then return.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Try to wait for all tasks (may block indefinitely in some cases)
        // g.tbb_graph.wait_for_all();
    }

    // In real app, we don't wait_for_all, we let it run in background.
};

} // namespace easywork
