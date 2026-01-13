#pragma once

#include <vector>
#include <functional>
#include "runtime/types/type_system.h"
#include "runtime/types/type_converter.h"

namespace easywork {

// Type-erased converter function: takes a Packet, returns a converted Packet
using AnyCaster = std::function<Packet(const Packet&)>;

} // namespace easywork
