#pragma once

#include <cstddef>

#include "runtime/types/type_system.h"

namespace easywork {

constexpr size_t ID_FORWARD = hash_string("forward");
constexpr size_t ID_OPEN = hash_string("Open");
constexpr size_t ID_CLOSE = hash_string("Close");

enum class ErrorPolicy {
    FailFast = 0,
    SkipCurrentData = 1,
};

} // namespace easywork
