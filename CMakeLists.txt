cmake_minimum_required(VERSION 3.15)
project(easywork LANGUAGES CXX)

# Check C++20 support (String Literal as NTTP)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
int main() {
    constexpr auto s = \"test\"; // String literal as NTTP (C++20)
    return 0;
}
" HAVE_CXX20_NTTP)

if(NOT HAVE_CXX20_NTTP)
    message(FATAL_ERROR "EasyWork requires C++20 support (String Literal as NTTP). "
                        "Please use GCC 10+, Clang 12+, or MSVC 2019+")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# CMAKE_SKIP_BUILD_RPATH is removed to allow proper library finding during development.

message(STATUS "C++20 support confirmed")

# --- 1. Dependencies ---

# OpenCV (Required)
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV Version: ${OpenCV_VERSION}")

# Taskflow (Header-Only)
message(STATUS "Using Taskflow from extern/taskflow")
if(NOT TARGET Taskflow::Taskflow)
    add_library(Taskflow INTERFACE)
    target_include_directories(Taskflow INTERFACE extern) # extern/taskflow/taskflow.hpp -> #include <taskflow/taskflow.hpp>
    add_library(Taskflow::Taskflow ALIAS Taskflow)
endif()

# Threads (pthread)
find_package(Threads REQUIRED)

# spdlog (Force build from source for ABI compatibility)
include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.12.0
)
FetchContent_MakeAvailable(spdlog)
message(STATUS "spdlog Built from source")

# Python & Pybind11
find_package(Python3 3.8 COMPONENTS Interpreter Development REQUIRED)

# Try to find pybind11 installed via pip
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pybind11; import os; print(os.path.dirname(pybind11.__file__))"
    OUTPUT_VARIABLE PYBIND11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(PYBIND11_DIR)
    # Found pybind11 installed via pip
    get_filename_component(PYBIND11_DIR "${PYBIND11_DIR}" ABSOLUTE)
    message(STATUS "Found pybind11 via pip: ${PYBIND11_DIR}")
    # Add pybind11 to CMake module path
    list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_DIR}/share/cmake/pybind11")
endif()

find_package(pybind11 QUIET)

if(Python3_EXECUTABLE)
    get_filename_component(PYTHON3_BIN_DIR "${Python3_EXECUTABLE}" DIRECTORY)
    get_filename_component(PYTHON3_PREFIX "${PYTHON3_BIN_DIR}" DIRECTORY)
    set(PYTHON3_LIBDIR "${PYTHON3_PREFIX}/lib")
    if(EXISTS "${PYTHON3_LIBDIR}")
        list(APPEND CMAKE_BUILD_RPATH "${PYTHON3_LIBDIR}")
        list(APPEND CMAKE_INSTALL_RPATH "${PYTHON3_LIBDIR}")
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
        link_directories("${PYTHON3_LIBDIR}")
        message(STATUS "Python3 libdir: ${PYTHON3_LIBDIR}")
    endif()
endif()

if(NOT pybind11_FOUND)
    # Fallback to FetchContent only if system pybind11 is not available
    message(STATUS "pybind11 not found, will download...")
    include(FetchContent)
    FetchContent_Declare(
      pybind11
      GIT_REPOSITORY https://github.com/pybind/pybind11.git
      GIT_TAG        v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# --- 2. Build Configuration ---

# Include Directories
include_directories(
    src
    extern
    extern/taskflow
    ${OpenCV_INCLUDE_DIRS}
)

# Source Files
file(GLOB_RECURSE SOURCES 
    "src/runtime/*.cpp" 
    "src/bindings/*.cpp"
)

# Runtime Library
add_library(easywork_runtime STATIC
    src/runtime/type_system.cpp
    # Add other runtime source files if they exist (currently core.h etc are header-only or templated)
)
target_include_directories(easywork_runtime PUBLIC src)

# Main Extension Module
pybind11_add_module(easywork_core src/bindings/bindings.cpp)
target_link_libraries(easywork_core PRIVATE 
    easywork_runtime
    Taskflow::Taskflow 
    ${OpenCV_LIBS} 
    spdlog::spdlog
)

# Output directory
set_target_properties(easywork_core PROPERTIES 
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python/easywork
)

# --- 4. C++ Unit Tests ---
enable_testing()

add_executable(test_invoker src/runtime/test_invoker.cpp)
target_link_libraries(test_invoker PRIVATE
    easywork_runtime
    ${OpenCV_LIBS}
    spdlog::spdlog
    Threads::Threads
)
add_test(NAME test_invoker COMMAND test_invoker)
