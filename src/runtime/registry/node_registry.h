#pragma once

#include <any>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <concepts>
#include <functional>
#include <algorithm>
#include <tuple>
#include <utility>
#ifdef EASYWORK_ENABLE_PYBIND
#include <pybind11/pybind11.h>
#endif
#include "runtime/core/core.h"

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

#ifdef EASYWORK_ENABLE_PYBIND
using NodeCreator = std::function<std::shared_ptr<Node>(
    pybind11::args, pybind11::kwargs)>;
#endif

using NodeCreatorAny = std::function<std::shared_ptr<Node>(
    const std::vector<std::any>&, const std::unordered_map<std::string, std::any>&)>;

/**
 * @brief Singleton registry for creating nodes by name.
 * 
 * Stores factory functions (Creators) for all registered node types.
 * Supports creating nodes with pybind11 arguments/keyword arguments.
 */
class NodeRegistry {
public:
    static NodeRegistry& instance() {
        static NodeRegistry registry;
        return registry;
    }

    void RegisterAny(std::string_view name, NodeCreatorAny creator) {
        creators_any_[std::string(name)] = std::move(creator);
    }

    std::shared_ptr<Node> CreateAny(std::string_view name,
                                    const std::vector<std::any>& args,
                                    const std::unordered_map<std::string, std::any>& kwargs) const {
        auto it = creators_any_.find(std::string(name));
        if (it == creators_any_.end()) {
            throw std::runtime_error("Unknown node type: " + std::string(name));
        }
        return it->second(args, kwargs);
    }

#ifdef EASYWORK_ENABLE_PYBIND
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
#endif

    [[nodiscard]] std::vector<std::string> RegisteredNodes() const {
        std::vector<std::string> names;
#ifdef EASYWORK_ENABLE_PYBIND
        const auto& creators = creators_;
#else
        const auto& creators = creators_any_;
#endif
        names.reserve(creators.size());
        for (const auto& [name, _] : creators) {
            names.push_back(name);
        }
        return names;
    }

    [[nodiscard]] bool IsRegistered(std::string_view name) const {
#ifdef EASYWORK_ENABLE_PYBIND
        return creators_.contains(std::string(name));
#else
        return creators_any_.contains(std::string(name));
#endif
    }

private:
    NodeRegistry() = default;
    std::unordered_map<std::string, NodeCreatorAny> creators_any_;
#ifdef EASYWORK_ENABLE_PYBIND
    std::unordered_map<std::string, NodeCreator> creators_;
#endif
};

// ========== Parameter Extraction & Factory ==========

namespace detail {
#ifdef EASYWORK_ENABLE_PYBIND
    // Extract parameter from args/kwargs with default value
    template<typename T>
    T extract_arg(pybind11::args& args, pybind11::kwargs& kwargs,
                 const char* name, int index, T default_val) {
        if (args.size() > index) {
            try {
                return args[index].cast<T>();
            } catch (const std::exception&) {
                throw std::runtime_error("Failed to parse argument '" + std::string(name) + "'");
            }
        }
        if (kwargs && kwargs.contains(name)) {
            try {
                return kwargs[name].cast<T>();
            } catch (const std::exception&) {
                throw std::runtime_error("Failed to parse argument '" + std::string(name) + "'");
            }
        }
        return default_val;
    }
#endif

    template<typename T>
    T any_to(const std::any& value, const char* name) {
        if (value.type() == typeid(T)) {
            return std::any_cast<T>(value);
        }
        if constexpr (std::is_arithmetic_v<T>) {
            if (value.type() == typeid(bool)) {
                return static_cast<T>(std::any_cast<bool>(value));
            }
            if (value.type() == typeid(int64_t)) {
                return static_cast<T>(std::any_cast<int64_t>(value));
            }
            if (value.type() == typeid(double)) {
                return static_cast<T>(std::any_cast<double>(value));
            }
        }
        if constexpr (std::is_same_v<T, std::string>) {
            if (value.type() == typeid(const char*)) {
                return std::string(std::any_cast<const char*>(value));
            }
        }
        throw std::runtime_error("Failed to parse argument '" + std::string(name) + "'");
    }

    template<typename T>
    T extract_any_arg(const std::vector<std::any>& args,
                     const std::unordered_map<std::string, std::any>& kwargs,
                     const char* name, int index, T default_val) {
        if (static_cast<size_t>(index) < args.size()) {
            return any_to<T>(args[index], name);
        }
        auto it = kwargs.find(name);
        if (it != kwargs.end()) {
            return any_to<T>(it->second, name);
        }
        return default_val;
    }

    // Helper: Create node using unpacked arguments
#ifdef EASYWORK_ENABLE_PYBIND
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
        TypeUsageRegistrar<typename Args::Type...>::Do();
        (RegisterPythonType<typename Args::Type>(), ...);
        return CreateNodeWithArgsImpl<NodeT>(
            args, kwargs, std::make_index_sequence<sizeof...(Args)>{}, std::make_tuple(arg_defs...)
        );
    }
#endif

    template<typename NodeT, size_t... I, typename Tuple>
    std::shared_ptr<Node> CreateNodeWithAnyImpl(const std::vector<std::any>& args,
                                                const std::unordered_map<std::string, std::any>& kwargs,
                                                std::index_sequence<I...>, const Tuple& arg_defs) {
        return std::make_shared<NodeT>(
            extract_any_arg(args, kwargs, std::get<I>(arg_defs).name, I, std::get<I>(arg_defs).default_val)...
        );
    }

    template<typename NodeT, typename... Args>
    std::shared_ptr<Node> CreateNodeWithAny(const std::vector<std::any>& args,
                                            const std::unordered_map<std::string, std::any>& kwargs,
                                            Args... arg_defs) {
        TypeUsageRegistrar<typename Args::Type...>::Do();
        (RegisterPythonType<typename Args::Type>(), ...);
        return CreateNodeWithAnyImpl<NodeT>(
            args, kwargs, std::make_index_sequence<sizeof...(Args)>{}, std::make_tuple(arg_defs...)
        );
    }
}

// ========== Argument Descriptor ==========

/**
 * @brief Describes a named argument with a default value for node registration.
 */
template<typename T>
struct Arg {
    using Type = T;
    const char* name;
    T default_val;
    Arg(const char* n, T v) : name(n), default_val(v) {}
};

// ========== Node Creator Impl (Fallback for 0-args) ==========

template<NodeType NodeT>
struct NodeCreatorImpl {
#ifdef EASYWORK_ENABLE_PYBIND
    static std::shared_ptr<Node> Create(pybind11::args, pybind11::kwargs) {
        if constexpr (std::is_default_constructible_v<NodeT>) {
            return std::make_shared<NodeT>();
        } else {
             throw std::runtime_error("Node requires parameters but none were provided in registration.");
        }
    }
#endif
};

// ========== Node Registrar ==========

template<StringLiteral Name, NodeType NodeT>
class NodeRegistrar {
public:
    // Default constructor: Uses NodeCreatorImpl (for 0-arg nodes)
    NodeRegistrar() {
        NodeRegistry::instance().RegisterAny(
            Name.view(),
            [](const std::vector<std::any>& args,
               const std::unordered_map<std::string, std::any>& kwargs) -> std::shared_ptr<Node> {
                if constexpr (std::is_default_constructible_v<NodeT>) {
                    return std::make_shared<NodeT>();
                } else {
                    throw std::runtime_error("Node requires parameters but none were provided in registration.");
                }
            }
        );
#ifdef EASYWORK_ENABLE_PYBIND
        NodeRegistry::instance().Register(
            Name.view(),
            [](pybind11::args args, pybind11::kwargs kwargs) -> std::shared_ptr<Node> {
                return NodeCreatorImpl<NodeT>::Create(args, kwargs);
            }
        );
#endif
    }

    // Variadic constructor handles N-arg cases
    template<typename... Args>
    NodeRegistrar(Args... arg_defs) {
        NodeRegistry::instance().RegisterAny(
            Name.view(),
            [=](const std::vector<std::any>& args,
               const std::unordered_map<std::string, std::any>& kwargs) -> std::shared_ptr<Node> {
                return detail::CreateNodeWithAny<NodeT>(args, kwargs, arg_defs...);
            }
        );
#ifdef EASYWORK_ENABLE_PYBIND
        NodeRegistry::instance().Register(
            Name.view(),
            [=](pybind11::args args, pybind11::kwargs kwargs) -> std::shared_ptr<Node> {
                return detail::CreateNodeWithArgs<NodeT>(args, kwargs, arg_defs...);
            }
        );
#endif
    }
};

// ========== Registration Macro ==========

/**
 * @brief Registers a node class with the system.
 * 
 * @param Classname The C++ class name of the node.
 * @param PyName The string name exposed to Python.
 * @param ... Optional Arg("name", default_val) descriptors.
 * 
 * Example:
 *   EW_REGISTER_NODE(MyNode, "MyNode", Arg("factor", 1.0))
 */
#define EW_REGISTER_NODE(Classname, PyName, ...) \
    inline easywork::NodeRegistrar<PyName, Classname> \
        registrar_##Classname##_{__VA_ARGS__};

} // namespace easywork
