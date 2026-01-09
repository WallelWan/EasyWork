#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <concepts>
#include <functional>
#include <algorithm>
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

// 旧版本的 Concepts（暂时保留，后续会废弃）
template<typename T>
concept InputNodeType = NodeType<T> && std::derived_from<T, InputNode> && requires {
    { std::declval<T&>().forward(std::declval<tbb::flow_control*>()) } -> std::convertible_to<Frame>;
};

template<typename T>
concept FunctionNodeType = NodeType<T> && std::derived_from<T, FunctionNode> && requires {
    { std::declval<T&>().forward(std::declval<Frame>()) } -> std::convertible_to<Frame>;
};

// ========== Compile-time String Hashing (constexpr/consteval) ==========

// FNV-1a hash algorithm (constexpr/consteval compatible)
constexpr std::size_t hash_string(std::string_view str) noexcept {
    std::size_t hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<std::size_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ========== String Literal as NTTP ==========

template<std::size_t N>
struct StringLiteral {
    constexpr StringLiteral(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }

    char value[N];

    constexpr std::size_t size() const { return N - 1; }

    constexpr std::string_view view() const { return std::string_view(value, N - 1); }

    constexpr std::size_t hash() const { return hash_string(view()); }
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

// ========== Parameter Extractor (Black Magic) ==========

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
}

// ========== Node Creator Impl (Template Specialization) ==========

template<NodeType NodeT>
struct NodeCreatorImpl {
    static std::shared_ptr<Node> Create(pybind11::args, pybind11::kwargs) {
        if constexpr (std::is_default_constructible_v<NodeT>) {
            return std::make_shared<NodeT>();
        } else {
            static_assert(std::is_default_constructible_v<NodeT>,
                "NodeType must be default constructible or use EW_REGISTER_NODE_ARGS macro");
        }
    }
};

// ========== Node Registrar (String Literal NTTP) ==========

template<StringLiteral Name, NodeType NodeT>
class NodeRegistrar {
public:
    NodeRegistrar() {
        NodeRegistry::instance().Register(
            Name.view(),
            [](pybind11::args args, pybind11::kwargs kwargs) -> std::shared_ptr<Node> {
                return NodeCreatorImpl<NodeT>::Create(args, kwargs);
            }
        );
    }
};

// ========== Registration Macros (Unified Interface) ==========

// For nodes with default constructor (0 parameters)
#define EW_REGISTER_NODE(Classname, PyName) \
    inline easywork::NodeRegistrar<PyName, Classname> \
        registrar_##Classname##_;

// For nodes with 1 parameter
#define EW_REGISTER_NODE_1(Classname, PyName, ParamType1, ParamName1, DefaultVal1) \
    template<> struct NodeCreatorImpl<Classname> { \
        static std::shared_ptr<Node> Create(pybind11::args args, pybind11::kwargs kwargs) { \
            ParamType1 p1 = easywork::detail::extract_arg<ParamType1>( \
                args, kwargs, #ParamName1, 0, DefaultVal1); \
            return std::make_shared<Classname>(p1); \
        } \
    }; \
    EW_REGISTER_NODE(Classname, PyName)

// For nodes with 2 parameters
#define EW_REGISTER_NODE_2(Classname, PyName, ParamType1, ParamName1, DefaultVal1, \
                           ParamType2, ParamName2, DefaultVal2) \
    template<> struct NodeCreatorImpl<Classname> { \
        static std::shared_ptr<Node> Create(pybind11::args args, pybind11::kwargs kwargs) { \
            ParamType1 p1 = easywork::detail::extract_arg<ParamType1>( \
                args, kwargs, #ParamName1, 0, DefaultVal1); \
            ParamType2 p2 = easywork::detail::extract_arg<ParamType2>( \
                args, kwargs, #ParamName2, 1, DefaultVal2); \
            return std::make_shared<Classname>(p1, p2); \
        } \
    }; \
    EW_REGISTER_NODE(Classname, PyName)

// For nodes with 3 parameters
#define EW_REGISTER_NODE_3(Classname, PyName, ParamType1, ParamName1, DefaultVal1, \
                           ParamType2, ParamName2, DefaultVal2, \
                           ParamType3, ParamName3, DefaultVal3) \
    template<> struct NodeCreatorImpl<Classname> { \
        static std::shared_ptr<Node> Create(pybind11::args args, pybind11::kwargs kwargs) { \
            ParamType1 p1 = easywork::detail::extract_arg<ParamType1>( \
                args, kwargs, #ParamName1, 0, DefaultVal1); \
            ParamType2 p2 = easywork::detail::extract_arg<ParamType2>( \
                args, kwargs, #ParamName2, 1, DefaultVal2); \
            ParamType3 p3 = easywork::detail::extract_arg<ParamType3>( \
                args, kwargs, #ParamName3, 2, DefaultVal3); \
            return std::make_shared<Classname>(p1, p2, p3); \
        } \
    }; \
    EW_REGISTER_NODE(Classname, PyName)

} // namespace easywork
