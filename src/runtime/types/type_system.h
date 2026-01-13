#pragma once

#include <any>
#include <typeinfo>
#include <typeindex>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <cstring>
#include <type_traits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#ifdef EASYWORK_ENABLE_PYBIND
#include <pybind11/pybind11.h>
#include "type_converter.h"
#endif

#ifdef __GNUG__
#include <cstdlib>
#include <cxxabi.h>
#endif

namespace easywork {

// ========== Demangling ==========

inline std::string demangle(const char* name) {
#ifdef __GNUG__
    int status = -4; // some arbitrary value to eliminate compiler warning
    std::unique_ptr<char, void(*)(void*)> res {
        abi::__cxa_demangle(name, NULL, NULL, &status),
        std::free
    };
    return (status == 0) ? res.get() : name;
#else
    return name;
#endif
}

// ========== Compile-time String Hashing ==========

constexpr std::size_t hash_string(std::string_view str) noexcept {
    std::size_t hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<std::size_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ========== TypeInfo ==========
// Type descriptor, stores runtime information of a type.
struct TypeInfo {
    const std::type_info* type_info;
    std::type_index type_index;
    std::string type_name;

    TypeInfo() : type_info(&typeid(void)), type_index(typeid(void)), type_name("void") {}

    template<typename T>
    static TypeInfo create() {
        return from(typeid(T));
    }

    static TypeInfo from(const std::type_info& info) {
        TypeInfo out;
        out.type_info = &info;
        out.type_index = std::type_index(info);
        out.type_name = demangle(info.name());
        return out;
    }

    bool operator==(const TypeInfo& other) const {
        return type_index == other.type_index;
    }

    bool operator!=(const TypeInfo& other) const {
        return !(*this == other);
    }
};

// ========== NodeTypeInfo ==========

struct MethodInfo {
    std::vector<TypeInfo> input_types;
    TypeInfo output_type;
};

struct NodeTypeInfo {
    std::unordered_map<size_t, MethodInfo> methods;

    bool accepts_input(size_t method_id, const std::vector<TypeInfo>& types) const {
        auto it = methods.find(method_id);
        if (it == methods.end()) return false;
        if (it->second.input_types.size() != types.size()) return false;
        return std::equal(types.begin(), types.end(), it->second.input_types.begin());
    }

    bool output_matches(size_t method_id, const TypeInfo& type) const {
        auto it = methods.find(method_id);
        if (it == methods.end()) return false;
        return it->second.output_type == type;
    }
};

// ========== Type Usage Registry ==========

inline std::unordered_set<std::type_index>& TypeUsageRegistry() {
    static std::unordered_set<std::type_index> registry;
    return registry;
}

inline std::mutex& TypeUsageMutex() {
    static std::mutex mutex;
    return mutex;
}

#ifdef EASYWORK_ENABLE_PYBIND
using PyConverter = std::function<pybind11::object(const std::any&)>;

inline std::unordered_map<std::type_index, PyConverter>& AnyToPyRegistry() {
    static std::unordered_map<std::type_index, PyConverter> registry;
    return registry;
}

template<typename T>
inline void RegisterPythonType() {
    const std::type_index type_key(typeid(T));
    auto& registry = AnyToPyRegistry();
    if (!registry.contains(type_key)) {
        registry.emplace(type_key, [](const std::any& value) {
            if constexpr (std::is_same_v<T, pybind11::object>) {
                return std::any_cast<const T&>(value);
            } else {
                return pybind11::cast(std::any_cast<const T&>(value));
            }
        });
    }
    TypeConverterRegistry::instance().register_converter<pybind11::object, T>(
        [](const pybind11::object& obj) { return obj.cast<T>(); });
}
#else
template<typename T>
inline void RegisterPythonType() {}
#endif

template<typename T>
inline void RegisterTypeUsage() {
    std::lock_guard<std::mutex> lock(TypeUsageMutex());
    TypeUsageRegistry().insert(std::type_index(typeid(std::decay_t<T>)));
}

template<typename... Ts>
struct TypeUsageRegistrar {
    static void Do() {
        (RegisterTypeUsage<Ts>(), ...);
    }
};

template<typename Derived, typename Ret, typename... Args>
inline void RegisterMethodTypes(Ret (Derived::*)(Args...)) {
    TypeUsageRegistrar<std::decay_t<Args>...>::Do();
    (RegisterPythonType<std::decay_t<Args>>(), ...);
    if constexpr (!std::is_void_v<Ret>) {
        RegisterTypeUsage<std::decay_t<Ret>>();
        RegisterPythonType<std::decay_t<Ret>>();
    }
}

// ========== Packet ==========

struct Packet {
    std::shared_ptr<std::any> payload;
    int64_t timestamp{0};

    Packet() = default;

    Packet(std::shared_ptr<std::any> value, int64_t ts)
        : payload(std::move(value)), timestamp(ts) {}

    bool has_value() const {
        return payload && payload->has_value();
    }

    TypeInfo type() const {
        return payload ? TypeInfo::from(payload->type()) : TypeInfo::create<void>();
    }

    const std::any& data() const {
        if (!payload) {
            throw std::runtime_error("Cannot access empty Packet payload");
        }
        return *payload;
    }

    template<typename T>
    T cast() const {
        if (!payload || !payload->has_value()) {
            throw std::runtime_error("Cannot cast empty Packet");
        }
        if (payload->type() != typeid(T)) {
            throw std::runtime_error(
                "Type mismatch: expected " + TypeInfo::create<T>().type_name +
                ", got " + TypeInfo::from(payload->type()).type_name
            );
        }
        return std::any_cast<T>(*payload);
    }

    static Packet Empty() {
        return Packet();
    }

    template<typename T>
    static Packet From(T&& val, int64_t ts) {
        return Packet(std::make_shared<std::any>(std::forward<T>(val)), ts);
    }

    static Packet FromAny(std::any val, int64_t ts) {
        return Packet(std::make_shared<std::any>(std::move(val)), ts);
    }

    static int64_t NowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace easywork
