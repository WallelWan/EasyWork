#pragma once

#include <typeinfo>
#include <string>
#include <vector>
#include <functional>
#include <cstring>
#include <type_traits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace easywork {

// ========== TypeInfo ==========
// 类型描述符，存储类型的运行时信息
struct TypeInfo {
    const std::type_info* type_info;  // 改为指针
    std::string type_name;
    size_t type_hash;

    // 默认构造函数
    TypeInfo() : type_info(&typeid(void)), type_name("void"), type_hash(0) {}

    // 拷贝构造函数
    TypeInfo(const TypeInfo&) = default;

    // 拷贝赋值运算符
    TypeInfo& operator=(const TypeInfo&) = default;

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

private:
    // FNV-1a hash 算法
    static constexpr size_t hash_string(std::string_view str) noexcept {
        size_t hash = 14695981039346656037ULL;
        for (char c : str) {
            hash ^= static_cast<size_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};

// ========== NodeTypeInfo ==========
// 节点类型元数据，描述输入输出类型
struct NodeTypeInfo {
    std::vector<TypeInfo> input_types;   // 空表示 InputNode
    std::vector<TypeInfo> output_types;  // 单个或多个输出

    // 检查是否接受某种输入类型
    bool accepts_input(const TypeInfo& type) const {
        if (input_types.empty()) return false;
        return std::any_of(input_types.begin(), input_types.end(),
            [&type](const TypeInfo& accepted) {
                return accepted == type;
            });
    }

    // 检查输出是否匹配某种类型
    bool output_matches(const TypeInfo& type) const {
        if (output_types.size() != 1) return false;
        return output_types[0] == type;
    }
};

// ========== Value ==========
// 类型擦除的通用值容器
// 使用 small-buffer optimization 避免小类型的堆分配
class Value {
public:
    // 默认构造
    Value() : is_small_(false), has_value_(false) {
        data_ = nullptr;
        type_info_ = TypeInfo::create<void>();
        destroy_fn_ = nullptr;
        copy_fn_ = nullptr;
        move_fn_ = nullptr;
    }

    // 从任意类型构造
    template<typename T>
    Value(T&& val) {
        using CleanT = std::decay_t<T>;

        // 检查是否适合 small-buffer optimization
        if constexpr (sizeof(CleanT) <= sizeof(buffer_) &&
                      alignof(CleanT) <= alignof(decltype(buffer_))) {
            // 使用小缓冲区
            new (&buffer_) CleanT(std::forward<T>(val));
            is_small_ = true;
            destroy_fn_ = &Destroy<CleanT>;
            copy_fn_ = &Copy<CleanT>;
            move_fn_ = &Move<CleanT>;
        } else {
            // 使用堆分配
            data_ = std::make_unique<Model<CleanT>>(std::forward<T>(val));
            is_small_ = false;
            destroy_fn_ = nullptr;
            copy_fn_ = nullptr;
            move_fn_ = nullptr;
        }

        type_info_ = TypeInfo::create<CleanT>();
        has_value_ = true;
    }

    // 拷贝构造
    Value(const Value& other)
        : is_small_(other.is_small_),
          has_value_(other.has_value_),
          destroy_fn_(other.destroy_fn_),
          copy_fn_(other.copy_fn_),
          move_fn_(other.move_fn_) {
        if (!has_value_) return;

        type_info_ = other.type_info_;

        if (is_small_) {
            // 拷贝小缓冲区
            if (copy_fn_) {
                copy_fn_(&buffer_, &other.buffer_);
            }
        } else {
            // 深拷贝堆对象
            data_ = other.data_->clone();
        }
    }

    // 移动构造
    Value(Value&& other) noexcept
        : is_small_(other.is_small_),
          has_value_(other.has_value_),
          type_info_(other.type_info_),
          destroy_fn_(other.destroy_fn_),
          copy_fn_(other.copy_fn_),
          move_fn_(other.move_fn_) {
        if (!has_value_) {
            data_ = nullptr;
            return;
        }

        if (is_small_) {
            // 移动小缓冲区
            if (move_fn_) {
                move_fn_(&buffer_, &other.buffer_);
            }
        } else {
            // 移动堆指针
            data_ = std::move(other.data_);
        }

        other.has_value_ = false;
        other.destroy_fn_ = nullptr;
        other.copy_fn_ = nullptr;
        other.move_fn_ = nullptr;
    }

    // 析构
    ~Value() {
        cleanup();
    }

    // 赋值运算符
    Value& operator=(const Value& other) {
        if (this != &other) {
            cleanup();

            is_small_ = other.is_small_;
            has_value_ = other.has_value_;
            type_info_ = other.type_info_;
            destroy_fn_ = other.destroy_fn_;
            copy_fn_ = other.copy_fn_;
            move_fn_ = other.move_fn_;

            if (has_value_) {
                if (is_small_) {
                    if (copy_fn_) {
                        copy_fn_(&buffer_, &other.buffer_);
                    }
                } else {
                    data_ = other.data_->clone();
                }
            }
        }
        return *this;
    }

    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            cleanup();

            is_small_ = other.is_small_;
            has_value_ = other.has_value_;
            type_info_ = other.type_info_;
            destroy_fn_ = other.destroy_fn_;
            copy_fn_ = other.copy_fn_;
            move_fn_ = other.move_fn_;

            if (has_value_) {
                if (is_small_) {
                    if (move_fn_) {
                        move_fn_(&buffer_, &other.buffer_);
                    }
                } else {
                    data_ = std::move(other.data_);
                }
            }

            other.has_value_ = false;
            other.destroy_fn_ = nullptr;
            other.copy_fn_ = nullptr;
            other.move_fn_ = nullptr;
        }
        return *this;
    }

    // 类型转换
    template<typename T>
    T cast() const {
        if (!has_value_) {
            throw std::runtime_error("Cannot cast empty Value");
        }

        TypeInfo target_type = TypeInfo::create<T>();

        if (type_info_ != target_type) {
            throw std::runtime_error(
                "Type mismatch: cannot cast " + type_info_.type_name + " to " + target_type.type_name
            );
        }

        if (is_small_) {
            return *reinterpret_cast<const T*>(&buffer_);
        } else {
            return static_cast<const Model<T>*>(data_.get())->value;
        }
    }

    // 获取类型信息
    TypeInfo type() const {
        return type_info_;
    }

    // 检查是否有值
    bool has_value() const {
        return has_value_;
    }

    // 检查是否是小缓冲区优化
    bool is_small() const {
        return is_small_;
    }

private:
    // 概念基类（类型擦除接口）
    struct Concept {
        virtual ~Concept() = default;
        virtual std::unique_ptr<Concept> clone() const = 0;
    };

    // 模型实现（具体类型）
    template<typename T>
    struct Model : Concept {
        T value;

        Model(T v) : value(std::move(v)) {}

        std::unique_ptr<Concept> clone() const override {
            return std::make_unique<Model<T>>(value);
        }
    };

    // 清理资源
    void cleanup() {
        if (!has_value_) return;

        if (is_small_) {
            // 调用析构函数
            if (destroy_fn_) {
                destroy_fn_(&buffer_);
            }
        } else {
            // 堆对象会自动释放
            data_.reset();
        }

        has_value_ = false;
        data_.reset();
        destroy_fn_ = nullptr;
        copy_fn_ = nullptr;
        move_fn_ = nullptr;
    }

    // 成员变量
    std::aligned_storage_t<32> buffer_;  // 小缓冲区（32 字节）
    std::unique_ptr<Concept> data_;       // 堆对象
    TypeInfo type_info_;                   // 类型信息
    bool is_small_;                        // 是否使用小缓冲区
    bool has_value_;                       // 是否有值
    using DestroyFn = void(*)(void*);
    using CopyFn = void(*)(void*, const void*);
    using MoveFn = void(*)(void*, void*);
    DestroyFn destroy_fn_;
    CopyFn copy_fn_;
    MoveFn move_fn_;

    template<typename T>
    static void Destroy(void* ptr) {
        reinterpret_cast<T*>(ptr)->~T();
    }

    template<typename T>
    static void Copy(void* dest, const void* src) {
        new (dest) T(*reinterpret_cast<const T*>(src));
    }

    template<typename T>
    static void Move(void* dest, void* src) {
        new (dest) T(std::move(*reinterpret_cast<T*>(src)));
        reinterpret_cast<T*>(src)->~T();
    }
};

// ========== 辅助函数 ==========

// 创建 Value 的辅助函数
template<typename T>
inline Value make_value(T&& val) {
    return Value(std::forward<T>(val));
}

} // namespace easywork
