#pragma once

#include <vector>
#include <string>
#include <unordered_map>

// =========================================================================
// Preprocessor Metaprogramming Helpers (Variadic Map)
// =========================================================================

// Expands to the number of arguments (up to 10)
#define EW_NARGS(...) EW_NARGS_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define EW_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N

// Macro dispatcher - Indirection needed for correct expansion
#define EW_CONCAT(A, B) EW_CONCAT_IMPL(A, B)
#define EW_CONCAT_IMPL(A, B) A##B

#define EW_CALL(M, ...) M(__VA_ARGS__)

// For Loop / Map implementation
#define EW_FOR_EACH(MACRO, ...) \
    EW_CALL(EW_CONCAT(EW_FOR_EACH_, EW_NARGS(__VA_ARGS__)), MACRO, __VA_ARGS__)

#define EW_FOR_EACH_0(MACRO, ...)
#define EW_FOR_EACH_1(MACRO, x) MACRO(x)
#define EW_FOR_EACH_2(MACRO, x, ...) MACRO(x) EW_FOR_EACH_1(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_3(MACRO, x, ...) MACRO(x) EW_FOR_EACH_2(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_4(MACRO, x, ...) MACRO(x) EW_FOR_EACH_3(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_5(MACRO, x, ...) MACRO(x) EW_FOR_EACH_4(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_6(MACRO, x, ...) MACRO(x) EW_FOR_EACH_5(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_7(MACRO, x, ...) MACRO(x) EW_FOR_EACH_6(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_8(MACRO, x, ...) MACRO(x) EW_FOR_EACH_7(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_9(MACRO, x, ...) MACRO(x) EW_FOR_EACH_8(MACRO, __VA_ARGS__)
#define EW_FOR_EACH_10(MACRO, x, ...) MACRO(x) EW_FOR_EACH_9(MACRO, __VA_ARGS__)

// Helpers for specific tasks
#define EW_STRINGIFY_METHOD(x) #x,
// Use { hash("name"), &Self::name } format. 
#define EW_PAIR_METHOD_ENTRY(x) { easywork::hash_string(#x), &Self::x },

// =========================================================================
// Main Export Macro
// =========================================================================

/**
 * @brief Automatically generates exposed_methods() and method_table()
 * 
 * Usage:
 *   class MyNode : public TypedFunctionNode<MyNode, ...> {
 *       ...
 *       EW_EXPORT_METHODS(left, right)
 *   };
 */
#define EW_EXPORT_METHODS(...) \
    std::vector<std::string> exposed_methods() const override { \
        return { "forward", EW_FOR_EACH(EW_STRINGIFY_METHOD, __VA_ARGS__) }; \
    } \
    \
    static std::unordered_map<size_t, MethodSignature> method_table() { \
        return { \
            EW_FOR_EACH(EW_PAIR_METHOD_ENTRY, __VA_ARGS__) \
        }; \
    }

// =========================================================================
// New Heterogeneous Registration (Phase 2+)
// =========================================================================

#define EW_METHOD_META_ENTRY(x) \
    { \
        easywork::hash_string(#x), \
        easywork::MethodMeta{ \
            easywork::CreateInvoker(&Self::x), \
            easywork::GetArgTypes(&Self::x), \
            easywork::GetReturnType(&Self::x) \
        } \
    },

/**
 * @brief Registers methods with automatic type reflection and invoker generation.
 *        Used for the new heterogeneous BaseNode system.
 * 
 * Usage:
 *   class MyNode : public BaseNode<MyNode> {
 *       int process(int x) { ... }
 *       void config(float v) { ... }
 *       EW_ENABLE_METHODS(process, config)
 *   };
 */
#define EW_ENABLE_METHODS(...) \
    static const std::unordered_map<size_t, easywork::MethodMeta>& method_registry() { \
        static const std::unordered_map<size_t, easywork::MethodMeta> registry = { \
            EW_FOR_EACH(EW_METHOD_META_ENTRY, __VA_ARGS__) \
        }; \
        return registry; \
    } \
    \
    std::vector<std::string> exposed_methods() const override { \
        return { EW_FOR_EACH(EW_STRINGIFY_METHOD, __VA_ARGS__) }; \
    }
