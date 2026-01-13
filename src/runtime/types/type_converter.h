#pragma once

#include <any>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <mutex>
#include <type_traits>
#include "runtime/types/type_traits.h"

namespace easywork {

// Custom hash for pair of type_index
struct TypePairHash {
    size_t operator()(const std::pair<std::type_index, std::type_index>& pair) const {
        size_t h1 = pair.first.hash_code();
        size_t h2 = pair.second.hash_code();
        return h1 ^ (h2 << 1);
    }
};

// Type conversion function signature
using TypeConverter = std::function<std::any(const std::any&)>;

// Singleton registry for type converters
class TypeConverterRegistry {
public:
    static TypeConverterRegistry& instance() {
        static TypeConverterRegistry registry;
        return registry;
    }

    // Register a converter from From type to To type
    template<typename From, typename To, typename ConverterFunc>
    void register_converter(ConverterFunc&& converter) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::type_index from_index(typeid(From));
        std::type_index to_index(typeid(To));
        
        auto key = std::make_pair(from_index, to_index);
        converters_[key] = [converter](const std::any& from_any) -> std::any {
            try {
                const From& from = std::any_cast<From>(from_any);
                return std::any(converter(from));
            } catch (...) {
                return std::any();
            }
        };
    }

    // Try to convert from one type to another
    std::any convert(const std::any& from, const std::type_info& from_type, const std::type_info& to_type) {
        std::type_index from_index(from_type);
        std::type_index to_index(to_type);
        
        auto key = std::make_pair(from_index, to_index);
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = converters_.find(key);
            if (it != converters_.end()) {
                return it->second(from);
            }
        }
        
        return std::any();
    }

    // Check if a converter is registered
    bool has_converter(const std::type_info& from_type, const std::type_info& to_type) const {
        std::type_index from_index(from_type);
        std::type_index to_index(to_type);
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = std::make_pair(from_index, to_index);
        return converters_.find(key) != converters_.end();
    }

private:
    TypeConverterRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::pair<std::type_index, std::type_index>, TypeConverter, TypePairHash> converters_;
};

// Helper to register arithmetic conversions automatically
template<typename From, typename To>
struct AutoRegistrar {
    AutoRegistrar() {
        if constexpr (is_convertible_v<From, To>) {
            TypeConverterRegistry::instance().register_converter<From, To>(
                [](const From& from) -> To {
                    return convert_type<From, To>(from);
                }
            );
        }
    }
};

// Force registration of common arithmetic conversions
inline void RegisterArithmeticConversions() {
    static AutoRegistrar<int, double> reg_int_double;
    static AutoRegistrar<int, float> reg_int_float;
    static AutoRegistrar<int, int64_t> reg_int_int64;
    static AutoRegistrar<int64_t, double> reg_int64_double;
    static AutoRegistrar<int64_t, float> reg_int64_float;
    static AutoRegistrar<int64_t, int> reg_int64_int;
    static AutoRegistrar<float, double> reg_float_double;
    static AutoRegistrar<double, float> reg_double_float;
    static AutoRegistrar<double, int64_t> reg_double_int64;
}

} // namespace easywork
