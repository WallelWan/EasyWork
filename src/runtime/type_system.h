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

namespace easywork {

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
// 类型描述符，存储类型的运行时信息
struct TypeInfo {
    const std::type_info* type_info;
    std::string type_name;
    size_t type_hash;

    // 默认构造函数
    TypeInfo() : type_info(&typeid(void)), type_name("void"), type_hash(0) {}

    // 从类型创建 TypeInfo
    template<typename T>
    static TypeInfo create() {
        TypeInfo info;
        info.type_info = &typeid(T);
        info.type_name = std::string(typeid(T).name());
        info.type_hash = hash_string(typeid(T).name());
        return info;
    }

    // 相等比较
    bool operator==(const TypeInfo& other) const {
        return type_hash == other.type_hash || *type_info == *(other.type_info);
    }

    bool operator!=(const TypeInfo& other) const {
        return !(*this == other);
    }
};

// ========== NodeTypeInfo ==========
struct NodeTypeInfo {
    std::vector<TypeInfo> input_types;
    std::vector<TypeInfo> output_types;

    bool accepts_input(const TypeInfo& type) const {
        if (input_types.empty()) return false;
        return std::any_of(input_types.begin(), input_types.end(),
            [&type](const TypeInfo& accepted) {
                return accepted == type;
            });
    }

    bool output_matches(const TypeInfo& type) const {
        if (output_types.size() != 1) return false;
        return output_types[0] == type;
    }
};

// ========== Value ==========
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
        if (type_info_ != TypeInfo::create<T>()) {
             throw std::runtime_error(
                "Type mismatch: cannot cast " + type_info_.type_name + " to " + TypeInfo::create<T>().type_name
            );
        }
        return std::any_cast<T>(data_);
    }

    TypeInfo type() const { return type_info_; }
    bool has_value() const { return data_.has_value(); }

private:
    std::any data_;
    TypeInfo type_info_;
};

template<typename T>
inline Value make_value(T&& val) {
    return Value(std::forward<T>(val));
}

} // namespace easywork
