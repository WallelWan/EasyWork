#pragma once

#include <type_traits>
#include <any>
#include <typeindex>
#include <cstdint>

namespace easywork {

// ========== Type Converter Traits (Compile-Time) ==========

template<typename From, typename To>
struct Converter {
    static constexpr bool value = false;
    static To convert(const From& from) = delete;
};

// Arithmetic type promotions (safe conversions)
template<typename To>
struct Converter<int, To> {
    static constexpr bool value = std::is_arithmetic_v<To>;
    static To convert(const int& from) {
        if constexpr (std::is_same_v<To, double>) {
            return static_cast<double>(from);
        } else if constexpr (std::is_same_v<To, float>) {
            return static_cast<float>(from);
        } else if constexpr (std::is_same_v<To, int64_t>) {
            return static_cast<int64_t>(from);
        } else {
            return static_cast<To>(from);
        }
    }
};

template<typename To>
struct Converter<int64_t, To> {
    static constexpr bool value = std::is_arithmetic_v<To>;
    static To convert(const int64_t& from) {
        if constexpr (std::is_same_v<To, double>) {
            return static_cast<double>(from);
        } else if constexpr (std::is_same_v<To, float>) {
            return static_cast<float>(from);
        } else if constexpr (std::is_same_v<To, int>) {
            return static_cast<int>(from);
        } else {
            return static_cast<To>(from);
        }
    }
};

template<typename To>
struct Converter<float, To> {
    static constexpr bool value = std::is_arithmetic_v<To> && !std::is_same_v<To, int>;
    static To convert(const float& from) {
        if constexpr (std::is_same_v<To, double>) {
            return static_cast<double>(from);
        } else {
            return static_cast<To>(from);
        }
    }
};

template<typename To>
struct Converter<double, To> {
    static constexpr bool value = std::is_same_v<To, float> || std::is_same_v<To, int64_t>;
    static To convert(const double& from) {
        return static_cast<To>(from);
    }
};

// Helper to check if conversion is allowed
template<typename From, typename To>
inline constexpr bool is_convertible_v = Converter<From, To>::value;

// Helper to perform conversion
template<typename From, typename To>
inline To convert_type(const From& from) {
    return Converter<From, To>::convert(from);
}

} // namespace easywork
