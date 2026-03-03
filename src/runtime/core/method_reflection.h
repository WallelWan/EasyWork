#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "runtime/types/type_system.h"

namespace easywork {

class Node;

using MethodInvoker = std::function<Packet(Node*, const std::vector<Packet>&)>;
using FastInvoker = Packet (*)(Node*, const std::vector<Packet>&);

struct MethodMeta {
    MethodInvoker invoker;
    FastInvoker fast_invoker{nullptr};
    std::vector<TypeInfo> arg_types;
    TypeInfo return_type;
};

namespace detail {

template <typename ArgT>
decltype(auto) CastArg(const Packet& p, size_t index) {
    try {
        return p.cast<std::decay_t<ArgT>>();
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Argument " + std::to_string(index) + " type mismatch: expected " +
            TypeInfo::create<std::decay_t<ArgT>>().type_name + ", got " + p.type().type_name);
    }
}

template <typename Derived, typename Ret, typename... Args, size_t... I>
Packet InvokeImpl(Node* node,
                  Ret (Derived::*func)(Args...),
                  const std::vector<Packet>& inputs,
                  std::index_sequence<I...>) {
    auto* derived = static_cast<Derived*>(node);

    if constexpr (std::is_void_v<Ret>) {
        (derived->*func)(CastArg<Args>(inputs[I], I)...);
        return Packet::Empty();
    } else {
        Ret result = (derived->*func)(CastArg<Args>(inputs[I], I)...);
        return Packet::From(std::move(result), 0);
    }
}

} // namespace detail

template <typename Derived, typename Ret, typename... Args>
MethodInvoker CreateInvoker(Ret (Derived::*func)(Args...)) {
    return [func](Node* base_node, const std::vector<Packet>& inputs) -> Packet {
        if (inputs.size() != sizeof...(Args)) {
            throw std::runtime_error(
                "Argument count mismatch: expected " + std::to_string(sizeof...(Args)) +
                ", got " + std::to_string(inputs.size()));
        }
        return detail::InvokeImpl<Derived, Ret, Args...>(
            base_node, func, inputs, std::make_index_sequence<sizeof...(Args)>{});
    };
}

namespace detail {

template <typename T>
inline constexpr bool kFastPathType = std::is_trivially_copyable_v<std::decay_t<T>>;

template <typename Ret, typename... Args>
inline constexpr bool kFastPathCompatible =
    (std::is_void_v<Ret> || kFastPathType<Ret>) && (kFastPathType<Args> && ...);

template <typename T>
struct MethodTraits;

template <typename Class, typename Ret, typename... Args>
struct MethodTraits<Ret (Class::*)(Args...)> {
    using ClassType = Class;
    using ReturnType = Ret;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t Arity = sizeof...(Args);
};

template <auto Func, typename Traits, size_t... I>
Packet FastInvokeImpl(Node* base_node, const std::vector<Packet>& inputs, std::index_sequence<I...>) {
    using Derived = typename Traits::ClassType;
    using Ret = typename Traits::ReturnType;
    if (inputs.size() != sizeof...(I)) {
        throw std::runtime_error(
            "Argument count mismatch: expected " + std::to_string(sizeof...(I)) +
            ", got " + std::to_string(inputs.size()));
    }
    return InvokeImpl<Derived, Ret, std::tuple_element_t<I, typename Traits::ArgsTuple>...>(
        base_node, Func, inputs, std::index_sequence<I...>{});
}

template <auto Func>
Packet FastInvoke(Node* base_node, const std::vector<Packet>& inputs) {
    using Traits = MethodTraits<decltype(Func)>;
    return FastInvokeImpl<Func, Traits>(base_node, inputs,
                                        std::make_index_sequence<Traits::Arity>{});
}

template <typename Traits, size_t... I>
constexpr bool FastPathCompatibleTuple(std::index_sequence<I...>) {
    return (std::is_void_v<typename Traits::ReturnType> ||
            kFastPathType<typename Traits::ReturnType>) &&
           (kFastPathType<std::tuple_element_t<I, typename Traits::ArgsTuple>> && ...);
}

} // namespace detail

template <auto Func>
FastInvoker CreateFastInvoker() {
    using Traits = detail::MethodTraits<decltype(Func)>;
    if constexpr (detail::FastPathCompatibleTuple<Traits>(std::make_index_sequence<Traits::Arity>{})) {
        return &detail::FastInvoke<Func>;
    }
    return nullptr;
}

template <typename Derived, typename Ret, typename... Args>
std::vector<TypeInfo> GetArgTypes(Ret (Derived::*)(Args...)) {
    return {TypeInfo::create<std::decay_t<Args>>()...};
}

template <typename Derived, typename Ret, typename... Args>
TypeInfo GetReturnType(Ret (Derived::*)(Args...)) {
    return TypeInfo::create<Ret>();
}

} // namespace easywork
