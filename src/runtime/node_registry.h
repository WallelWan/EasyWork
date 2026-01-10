#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <concepts>
#include <functional>
#include <algorithm>
#include <tuple>
#include <utility>
#include <pybind11/pybind11.h>
#include "core_tbb.h"

namespace easywork {

// ========== C++20 Concepts ==========

template<typename T>
concept NodeType = std::derived_from<T, Node> && requires(ExecutionGraph& g) {
    { std::declval<T&>().build(g) } -> std::same_as<void>;
    { std::declval<T&>().connect() } -> std::same_as<void>;
    { std::declval<T&>().get_type_info() } -> std::convertible_to<NodeTypeInfo>;
};

// ========== String Literal as NTTP ==========

template<std::size_t N>
struct StringLiteral {
    constexpr StringLiteral(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }
    char value[N];
    constexpr std::size_t size() const { return N - 1; }
    constexpr std::string_view view() const { return std::string_view(value, N - 1); }
};

// ========== Node Registry ==========

using NodeCreator = std::function<std::shared_ptr<Node>(
    pybind11::args, pybind11::kwargs)>;

class NodeRegistry {
public:
    static NodeRegistry& instance() {
        static NodeRegistry registry;
        return registry;
    }

    void Register(std::string_view name, NodeCreator creator) {
        creators_[std::string(name)] = std::move(creator);
    }

    std::shared_ptr<Node> Create(std::string_view name,
                                 pybind11::args args,
                                 pybind11::kwargs kwargs) const {
        auto it = creators_.find(std::string(name));
        if (it == creators_.end()) {
            throw std::runtime_error("Unknown node type: " + std::string(name));
        }
        return it->second(args, kwargs);
    }

    [[nodiscard]] std::vector<std::string> RegisteredNodes() const {
        std::vector<std::string> names;
        names.reserve(creators_.size());
        for (const auto& [name, _] : creators_) {
            names.push_back(name);
        }
        return names;
    }

    [[nodiscard]] bool IsRegistered(std::string_view name) const {
        return creators_.contains(std::string(name));
    }

private:
    NodeRegistry() = default;
    std::unordered_map<std::string, NodeCreator> creators_;
};

// ========== Parameter Extraction & Factory ==========

namespace detail {
    // Extract parameter from args/kwargs with default value
    template<typename T>
    T extract_arg(pybind11::args& args, pybind11::kwargs& kwargs,
                 const char* name, int index, T default_val) {
        if (args.size() > index) {
            try {
                return args[index].cast<T>();
            } catch (...) {
                return default_val;
            }
        }
        if (kwargs && kwargs.contains(name)) {
            try {
                return kwargs[name].cast<T>();
            } catch (...) {
                return default_val;
            }
        }
        return default_val;
    }

    // Helper: Create node using unpacked arguments
    template<typename NodeT, size_t... I, typename Tuple>
    std::shared_ptr<Node> CreateNodeWithArgsImpl(pybind11::args& args, pybind11::kwargs& kwargs,
                                                 std::index_sequence<I...>, const Tuple& arg_defs) {
        return std::make_shared<NodeT>(
            extract_arg(args, kwargs, std::get<I>(arg_defs).name, I, std::get<I>(arg_defs).default_val)...
        );
    }

    // Entry point: Takes variadic Args, packs them into a tuple, and delegates
    template<typename NodeT, typename... Args>
    std::shared_ptr<Node> CreateNodeWithArgs(pybind11::args args, pybind11::kwargs kwargs, Args... arg_defs) {
        return CreateNodeWithArgsImpl<NodeT>(
            args, kwargs, std::make_index_sequence<sizeof...(Args)>{}, std::make_tuple(arg_defs...)
        );
    }
}

// ========== Argument Descriptor ==========

template<typename T>
struct Arg {
    const char* name;
    T default_val;
    Arg(const char* n, T v) : name(n), default_val(v) {}
};

// ========== Node Creator Impl (Fallback for 0-args) ==========

template<NodeType NodeT>
struct NodeCreatorImpl {
    static std::shared_ptr<Node> Create(pybind11::args, pybind11::kwargs) {
        if constexpr (std::is_default_constructible_v<NodeT>) {
            return std::make_shared<NodeT>();
        } else {
             throw std::runtime_error("Node requires parameters but none were provided in registration.");
        }
    }
};

// ========== Node Registrar ==========

template<StringLiteral Name, NodeType NodeT>
class NodeRegistrar {
public:
    // Default constructor: Uses NodeCreatorImpl (for 0-arg nodes)
    NodeRegistrar() {
        NodeRegistry::instance().Register(
            Name.view(),
            [](pybind11::args args, pybind11::kwargs kwargs) -> std::shared_ptr<Node> {
                return NodeCreatorImpl<NodeT>::Create(args, kwargs);
            }
        );
    }

    // Variadic constructor handles N-arg cases
    template<typename... Args>
    NodeRegistrar(Args... arg_defs) {
        NodeRegistry::instance().Register(
            Name.view(),
            [=](pybind11::args args, pybind11::kwargs kwargs) -> std::shared_ptr<Node> {
                return detail::CreateNodeWithArgs<NodeT>(args, kwargs, arg_defs...);
            }
        );
    }
};

// ========== Registration Macro ==========

// Unified macro: EW_REGISTER_NODE(Class, "Name", Arg(...), ...)
#define EW_REGISTER_NODE(Classname, PyName, ...) \
    inline easywork::NodeRegistrar<PyName, Classname> \
        registrar_##Classname##_{__VA_ARGS__};

} // namespace easywork
