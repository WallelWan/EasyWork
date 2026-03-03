#pragma once

#include <functional>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "runtime/core/method_reflection.h"
#include "runtime/core/node.h"
#include "runtime/registry/macros.h"

namespace easywork {

namespace detail {
template <typename T>
inline constexpr bool kHasMethodRegistry = requires {
    { T::method_registry() };
};
} // namespace detail

template <typename TupleT>
bool RegisterTupleType();

template <typename Derived>
class BaseNode : public Node {
public:
    using Self = Derived;

    BaseNode() = default;

    void build(ExecutionGraph& g) override {
        graph_ = &g;
        g.RegisterNode(this);
        task_ = g.taskflow.emplace([this]() { RunDispatch(); });
        task_.name(TypeInfo::create<Derived>().type_name);
    }

    NodeTypeInfo get_type_info() const override {
        NodeTypeInfo info;
        const auto& registry = Derived::method_registry();
        for (const auto& [id, meta] : registry) {
            info.methods[id] = {meta.arg_types, meta.return_type};
        }
        return info;
    }

    std::string type_name() const override {
        return TypeInfo::create<Derived>().type_name;
    }

    void connect() override {
        for (const auto& conn : UpstreamConnections()) {
            if (conn.node && !conn.weak) {
                conn.node->get_task().precede(task_);
            }
        }
        PrepareInputConverters();
        BuildDispatchPlan();
    }

    Packet invoke(size_t method_id, const std::vector<Packet>& inputs) override {
        const auto& registry = Derived::method_registry();
        auto it = registry.find(method_id);
        if (it == registry.end()) {
            throw std::runtime_error("Method not found in registry: " + std::to_string(method_id));
        }
        return it->second.invoker(this, inputs);
    }

    bool HasMethod(size_t method_id) const override {
        const auto& registry = Derived::method_registry();
        return registry.find(method_id) != registry.end();
    }

protected:
    struct MethodInvokerPlan {
        const MethodInvoker* invoker{nullptr};
        FastInvoker fast_invoker{nullptr};
    };

    void PrepareInputConverters() {
        RegisterArithmeticConversions();
        auto& converters = MutablePortConverters();
        converters.clear();
        port_converters_fast_.clear();
        port_has_converter_.clear();

        const auto& registry = Derived::method_registry();
        for (const auto& [method_id, meta] : registry) {
            for (size_t i = 0; i < meta.arg_types.size(); ++i) {
                const TypeInfo& to_type = meta.arg_types[i];

                std::vector<Node*> potential_sources;
                auto mux_method_it = MuxConfigs().find(method_id);
                if (mux_method_it != MuxConfigs().end()) {
                    auto mux_arg_it = mux_method_it->second.find(static_cast<int>(i));
                    if (mux_arg_it != mux_method_it->second.end()) {
                        for (const auto& pair : mux_arg_it->second.map) {
                            potential_sources.push_back(pair.second);
                        }
                    }
                }
                if (potential_sources.empty()) {
                    for (const auto& u : UpstreamConnections()) {
                        int idx = FindPortIndex(u.node, method_id, static_cast<int>(i));
                        if (idx != -1) {
                            potential_sources.push_back(u.node);
                            break;
                        }
                    }
                }

                for (Node* src : potential_sources) {
                    int port_index = FindPortIndex(src, method_id, static_cast<int>(i));
                    if (port_index == -1) {
                        continue;
                    }

                    TypeInfo from_type = UpstreamOutputType(src);
                    if (from_type == to_type) {
                        continue;
                    }

                    if (!TypeConverterRegistry::instance().has_converter(*from_type.type_info,
                                                                         *to_type.type_info)) {
                        throw std::runtime_error("Type mismatch: cannot connect " + from_type.type_name +
                                                 " to " + to_type.type_name);
                    }

                    converters[static_cast<size_t>(port_index)] = [from_type, to_type](const Packet& packet) {
                        if (!packet.has_value()) {
                            return Packet::Empty();
                        }
                        std::any converted = TypeConverterRegistry::instance().convert(
                            packet.data(), *from_type.type_info, *to_type.type_info);
                        if (!converted.has_value()) {
                            throw std::runtime_error("Failed to convert " + from_type.type_name + " to " +
                                                     to_type.type_name);
                        }
                        return Packet::FromAny(std::move(converted), packet.timestamp);
                    };
                }
            }
        }

        const size_t port_count = UpstreamConnections().size();
        port_converters_fast_.resize(port_count);
        port_has_converter_.assign(port_count, 0);
        for (const auto& [port_idx, converter] : PortConverters()) {
            if (port_idx < port_count) {
                port_converters_fast_[port_idx] = converter;
                port_has_converter_[port_idx] = 1;
            }
        }
    }

    TypeInfo UpstreamOutputType(Node* node) const {
        if (!node) {
            return TypeInfo::create<void>();
        }
        NodeTypeInfo info = node->get_type_info();
        auto it = info.methods.find(ID_FORWARD);
        if (it == info.methods.end()) {
            return TypeInfo::create<void>();
        }
        return it->second.output_type;
    }

    void BuildDispatchPlan() {
        dispatch_plans_.clear();
        invoker_plans_.clear();

        std::vector<std::pair<size_t, size_t>> method_specs;
        const auto& order = EffectiveMethodOrder();
        const auto& registry = Derived::method_registry();

        for (size_t method_id : order) {
            auto it = registry.find(method_id);
            if (it == registry.end()) {
                continue;
            }
            method_specs.emplace_back(method_id, it->second.arg_types.size());
            invoker_plans_.push_back({&it->second.invoker, it->second.fast_invoker});
        }

        BuildDispatchPlans(method_specs, &dispatch_plans_);
        plan_built_ = true;
    }

    void RunDispatch() {
        try {
            auto run_impl = [this]() {
                if (!plan_built_) {
                    BuildDispatchPlan();
                }

                RunDispatchPlans(
                    dispatch_plans_,
                    [](const Packet& control_pkt) { return DecodeMuxChoiceDefault(control_pkt); },
                    [this](int selected_port, const Packet& pkt) -> Packet {
                        if (selected_port >= 0 &&
                            static_cast<size_t>(selected_port) < port_has_converter_.size() &&
                            port_has_converter_[selected_port]) {
                            return port_converters_fast_[selected_port](pkt);
                        }
                        return pkt;
                    },
                    [this](size_t plan_index,
                           const DispatchMethodPlan&,
                           const std::vector<Packet>& inputs) -> Packet {
                        const auto& invoker_plan = invoker_plans_[plan_index];
                        if (invoker_plan.fast_invoker) {
                            return invoker_plan.fast_invoker(this, inputs);
                        }
                        if (invoker_plan.invoker) {
                            return (*invoker_plan.invoker)(this, inputs);
                        }
                        return Packet::Empty();
                    });
            };

            run_impl();
        } catch (const std::exception& e) {
            if (graph_) {
                graph_->ReportError(ErrorCode::DispatchError,
                                    std::string("Dispatch Error: ") + e.what(),
                                    {
                                        {"node", type_name()},
                                        {"event", "dispatch_exception"},
                                    });
            } else {
                std::cerr << "Dispatch Error: " << e.what() << std::endl;
            }
            ClearOutput();
        }
    }

    std::vector<DispatchMethodPlan> dispatch_plans_;
    std::vector<MethodInvokerPlan> invoker_plans_;
    bool plan_built_{false};
    std::vector<AnyCaster> port_converters_fast_;
    std::vector<uint8_t> port_has_converter_;
};

template <size_t Index, typename TupleT>
class TupleGetNode : public BaseNode<TupleGetNode<Index, TupleT>> {
public:
    using Self = TupleGetNode<Index, TupleT>;
    using ElementType = std::tuple_element_t<Index, TupleT>;

    ElementType forward(TupleT input) {
        return std::get<Index>(input);
    }

    EW_ENABLE_METHODS(forward)
};

namespace detail {

struct TupleRegistryEntry {
    size_t size;
    std::function<std::shared_ptr<Node>(size_t)> factory;
};

inline std::unordered_map<std::type_index, TupleRegistryEntry>& TupleRegistry() {
    static std::unordered_map<std::type_index, TupleRegistryEntry> registry;
    return registry;
}

inline std::mutex& TupleRegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

template <typename TupleT, size_t... Index>
std::shared_ptr<Node> CreateTupleGetNodeForIndex(size_t index, std::index_sequence<Index...>) {
    std::shared_ptr<Node> node;
    bool matched = ((index == Index ? (node = std::make_shared<TupleGetNode<Index, TupleT>>(), true)
                                    : false) ||
                    ...);
    (void)matched;
    if (!node) {
        throw std::runtime_error("Unsupported tuple index for TupleGetNode");
    }
    return node;
}

} // namespace detail

template <typename TupleT>
inline bool RegisterTupleType() {
    static_assert(std::tuple_size_v<TupleT> > 0, "Tuple type must not be empty");
    const auto type_info = TypeInfo::create<TupleT>();
    const auto type_key = type_info.type_index;
    std::lock_guard<std::mutex> lock(detail::TupleRegistryMutex());
    auto& registry = detail::TupleRegistry();
    if (registry.contains(type_key)) {
        return false;
    }
    detail::TupleRegistryEntry entry;
    entry.size = std::tuple_size_v<TupleT>;
    entry.factory = [](size_t index) {
        return detail::CreateTupleGetNodeForIndex<TupleT>(
            index, std::make_index_sequence<std::tuple_size_v<TupleT>>{});
    };
    registry.emplace(type_key, std::move(entry));
    return true;
}

inline std::shared_ptr<Node> CreateTupleGetNode(const TypeInfo& tuple_type, size_t index) {
    std::lock_guard<std::mutex> lock(detail::TupleRegistryMutex());
    auto& registry = detail::TupleRegistry();
    auto it = registry.find(tuple_type.type_index);
    if (it == registry.end()) {
        throw std::runtime_error("Tuple type not registered for TupleGetNode");
    }
    if (index >= it->second.size) {
        throw std::runtime_error("Tuple index out of range for TupleGetNode");
    }
    return it->second.factory(index);
}

inline size_t GetTupleSize(const TypeInfo& tuple_type) {
    std::lock_guard<std::mutex> lock(detail::TupleRegistryMutex());
    auto& registry = detail::TupleRegistry();
    auto it = registry.find(tuple_type.type_index);
    if (it == registry.end()) {
        return 0;
    }
    return it->second.size;
}

} // namespace easywork
