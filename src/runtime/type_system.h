#pragma once

#include <any>
#include <typeinfo>
#include <string>
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
    std::string type_name;
    size_t type_hash;

    // Default constructor
    TypeInfo() : type_info(&typeid(void)), type_name("void"), type_hash(0) {}

    // Create TypeInfo from type T
    template<typename T>
    static TypeInfo create() {
        TypeInfo info;
        info.type_info = &typeid(T);
        info.type_name = demangle(typeid(T).name());
        info.type_hash = hash_string(typeid(T).name());
        return info;
    }

    // Equality comparison
    bool operator==(const TypeInfo& other) const {
        return type_hash == other.type_hash || *type_info == *(other.type_info);
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
    // Map from Method ID (hash) to signature
    std::unordered_map<size_t, MethodInfo> methods;

    // Helper: Check if a specific method accepts these inputs
    bool accepts_input(size_t method_id, const std::vector<TypeInfo>& types) const {
        auto it = methods.find(method_id);
        if (it == methods.end()) return false;
        if (it->second.input_types.size() != types.size()) return false;
        return std::equal(types.begin(), types.end(), it->second.input_types.begin());
    }
    
    // Helper: Check output of a method
    bool output_matches(size_t method_id, const TypeInfo& type) const {
        auto it = methods.find(method_id);
        if (it == methods.end()) return false;
        return it->second.output_type == type;
    }
};

// ========== Value ==========

// Hook for Python-specific type conversion (initialized by bindings)
using PythonCastHook = std::any (*)(const std::any& src_data, const std::type_info& src_type, const std::type_info& target_type);
PythonCastHook& GetPythonCastHook();

class Value {
public:
    Value() : type_info_(TypeInfo::create<void>()) {}

    template<typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>>>
    Value(T&& val) 
        : data_(std::forward<T>(val)), 
          type_info_(TypeInfo::create<std::decay_t<T>>()) {}

    template<typename T>
    T cast() const {
        if (!has_value()) {
            throw std::runtime_error("Cannot cast empty Value");
        }
        
        // 1. Exact match
        if (type_info_ == TypeInfo::create<T>()) {
            return std::any_cast<T>(data_);
        }

        // 2. Python Automatic Coercion (if hook is available)
        auto hook = GetPythonCastHook();
        if (hook) {
            try {
                std::any converted = hook(data_, *type_info_.type_info, typeid(T));
                if (converted.has_value()) {
                    return std::any_cast<T>(converted);
                }
            } catch (...) {
                // Conversion failed, fall through to error
            }
        }
        
        // 3. Numeric Conversions

        // Target: int
        if constexpr (std::is_same_v<T, int>) {
            if (type_info_ == TypeInfo::create<int64_t>()) return static_cast<int>(std::any_cast<int64_t>(data_));
            if (type_info_ == TypeInfo::create<double>()) return static_cast<int>(std::any_cast<double>(data_));
            if (type_info_ == TypeInfo::create<float>()) return static_cast<int>(std::any_cast<float>(data_));
        }
        
        // Target: int64_t (Python int)
        if constexpr (std::is_same_v<T, int64_t>) {
            if (type_info_ == TypeInfo::create<int>()) return static_cast<int64_t>(std::any_cast<int>(data_));
            if (type_info_ == TypeInfo::create<double>()) return static_cast<int64_t>(std::any_cast<double>(data_));
        }

        // Target: double (Python float)
        if constexpr (std::is_same_v<T, double>) {
            if (type_info_ == TypeInfo::create<float>()) return static_cast<double>(std::any_cast<float>(data_));
            if (type_info_ == TypeInfo::create<int>()) return static_cast<double>(std::any_cast<int>(data_));
            if (type_info_ == TypeInfo::create<int64_t>()) return static_cast<double>(std::any_cast<int64_t>(data_));
        }

        // Target: float
        if constexpr (std::is_same_v<T, float>) {
            if (type_info_ == TypeInfo::create<double>()) return static_cast<float>(std::any_cast<double>(data_));
            if (type_info_ == TypeInfo::create<int>()) return static_cast<float>(std::any_cast<int>(data_));
            if (type_info_ == TypeInfo::create<int64_t>()) return static_cast<float>(std::any_cast<int64_t>(data_));
        }

        throw std::runtime_error(
            "Type mismatch: cannot cast " + type_info_.type_name + " to " + TypeInfo::create<T>().type_name
        );
    }

    TypeInfo type() const { return type_info_; }
    bool has_value() const { return data_.has_value(); }

private:
    std::any data_;
    TypeInfo type_info_;
};

struct Packet {
    std::shared_ptr<Value> payload;
    int64_t timestamp; // nanoseconds

    Packet() : payload(nullptr), timestamp(0) {}
    Packet(std::shared_ptr<Value> value, int64_t ts)
        : payload(std::move(value)), timestamp(ts) {}

    bool has_value() const {
        return payload && payload->has_value();
    }

    TypeInfo type() const {
        return payload ? payload->type() : TypeInfo::create<void>();
    }

    template<typename T>
    T cast() const {
        if (!payload) {
            throw std::runtime_error("Cannot cast empty Packet");
        }
        return payload->template cast<T>();
    }

    static Packet Empty() {
        return Packet();
    }

    template<typename T>
    static Packet From(T&& val, int64_t ts) {
        return Packet(std::make_shared<Value>(std::forward<T>(val)), ts);
    }

    static int64_t NowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

template<typename T>
inline Value make_value(T&& val) {
    return Value(std::forward<T>(val));
}

} // namespace easywork
