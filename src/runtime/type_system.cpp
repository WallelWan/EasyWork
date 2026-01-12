#include "type_system.h"

namespace easywork {

PythonCastHook& GetPythonCastHook() {
    static PythonCastHook hook = nullptr;
    return hook;
}

} // namespace easywork
