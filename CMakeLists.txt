cmake_minimum_required(VERSION 3.15)
project(easywork LANGUAGES CXX)

# Check C++20 support (String Literal as NTTP)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
int main() {
    constexpr auto s = \"test\";
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

option(EASYWORK_BUILD_PYTHON "Build Python bindings (easywork_core)" ON)
option(EASYWORK_WITH_OPENCV "Enable OpenCV-dependent modules" ON)
option(EASYWORK_BUILD_EXAMPLES "Build C++ examples and tools" ON)
option(EASYWORK_BUILD_TESTS "Build and register tests" ON)
option(EASYWORK_FETCH_DEPS "Allow FetchContent fallback for dependencies" ON)

message(STATUS "C++20 support confirmed")
message(STATUS "EASYWORK_BUILD_PYTHON=${EASYWORK_BUILD_PYTHON}")
message(STATUS "EASYWORK_WITH_OPENCV=${EASYWORK_WITH_OPENCV}")
message(STATUS "EASYWORK_BUILD_EXAMPLES=${EASYWORK_BUILD_EXAMPLES}")
message(STATUS "EASYWORK_BUILD_TESTS=${EASYWORK_BUILD_TESTS}")
message(STATUS "EASYWORK_FETCH_DEPS=${EASYWORK_FETCH_DEPS}")

# Taskflow (header-only, vendored)
if(NOT TARGET Taskflow::Taskflow)
    add_library(Taskflow INTERFACE)
    target_include_directories(Taskflow INTERFACE extern)
    add_library(Taskflow::Taskflow ALIAS Taskflow)
endif()

find_package(Threads REQUIRED)

# nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    if(EXISTS "${CMAKE_SOURCE_DIR}/build/_deps/nlohmann_json-src/CMakeLists.txt")
        add_subdirectory(
            "${CMAKE_SOURCE_DIR}/build/_deps/nlohmann_json-src"
            "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-local-build"
            EXCLUDE_FROM_ALL
        )
        if(TARGET nlohmann_json::nlohmann_json)
            set(nlohmann_json_FOUND ON)
        endif()
    endif()
endif()

if(NOT nlohmann_json_FOUND)
    if(NOT EASYWORK_FETCH_DEPS)
        message(FATAL_ERROR "nlohmann_json not found and EASYWORK_FETCH_DEPS=OFF")
    endif()
    include(FetchContent)
    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Optional OpenCV target bucket
if(EASYWORK_WITH_OPENCV)
    find_package(OpenCV REQUIRED)
    add_library(easywork_modules_cv INTERFACE)
    target_link_libraries(easywork_modules_cv INTERFACE ${OpenCV_LIBS})
    target_include_directories(easywork_modules_cv INTERFACE ${OpenCV_INCLUDE_DIRS})
    message(STATUS "OpenCV enabled: ${OpenCV_VERSION}")
else()
    add_library(easywork_modules_cv INTERFACE)
    message(STATUS "OpenCV disabled")
endif()

# Runtime core library (no Python/OpenCV required)
add_library(easywork_runtime STATIC
    src/runtime/graph_build.cpp
)
add_library(easywork::runtime ALIAS easywork_runtime)

target_include_directories(easywork_runtime
    PUBLIC
        src
)

target_link_libraries(easywork_runtime
    PUBLIC
        Taskflow::Taskflow
        Threads::Threads
        nlohmann_json::nlohmann_json
)

if(EASYWORK_WITH_OPENCV)
    target_compile_definitions(easywork_runtime PUBLIC EASYWORK_WITH_OPENCV=1)
else()
    target_compile_definitions(easywork_runtime PUBLIC EASYWORK_WITH_OPENCV=0)
endif()

# Python bindings
if(EASYWORK_BUILD_PYTHON)
    find_package(Python3 3.8 COMPONENTS Interpreter Development REQUIRED)

    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import pybind11, os; print(os.path.dirname(pybind11.__file__))"
        OUTPUT_VARIABLE PYBIND11_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(PYBIND11_DIR)
        get_filename_component(PYBIND11_DIR "${PYBIND11_DIR}" ABSOLUTE)
        list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_DIR}/share/cmake/pybind11")
    endif()

    find_package(pybind11 QUIET)
    if(NOT pybind11_FOUND)
        if(EXISTS "${CMAKE_SOURCE_DIR}/build/_deps/pybind11-src/CMakeLists.txt")
            add_subdirectory(
                "${CMAKE_SOURCE_DIR}/build/_deps/pybind11-src"
                "${CMAKE_BINARY_DIR}/_deps/pybind11-local-build"
                EXCLUDE_FROM_ALL
            )
            if(TARGET pybind11::module)
                set(pybind11_FOUND ON)
            endif()
        endif()
    endif()

    if(NOT pybind11_FOUND)
        if(NOT EASYWORK_FETCH_DEPS)
            message(FATAL_ERROR "pybind11 not found and EASYWORK_FETCH_DEPS=OFF")
        endif()
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.11.1
        )
        FetchContent_MakeAvailable(pybind11)
    endif()

    pybind11_add_module(easywork_core
        src/bindings/bindings.cpp
    )
    target_compile_definitions(easywork_core PRIVATE EASYWORK_ENABLE_PYBIND)
    target_link_libraries(easywork_core PRIVATE easywork_runtime)
    set_target_properties(easywork_core PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python/easywork
    )
endif()

if(EASYWORK_BUILD_EXAMPLES)
    add_executable(easywork_runtime_example
        examples/runtime_minimal.cpp
    )
    target_link_libraries(easywork_runtime_example PRIVATE easywork_runtime)

    add_executable(easywork-run
        apps/easywork_run.cpp
    )
    target_link_libraries(easywork-run PRIVATE easywork_runtime)
endif()

if(EASYWORK_BUILD_TESTS)
    enable_testing()

    add_executable(easywork_graph_build_test
        tests/cpp/graph_build_test.cpp
    )
    target_link_libraries(easywork_graph_build_test PRIVATE easywork_runtime)

    add_test(
        NAME GraphBuildCppTest
        COMMAND easywork_graph_build_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(easywork_runtime_contract_test
        tests/cpp/runtime_contract_test.cpp
    )
    target_link_libraries(easywork_runtime_contract_test PRIVATE easywork_runtime)

    add_test(
        NAME RuntimeContractCppTest
        COMMAND easywork_runtime_contract_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(EASYWORK_BUILD_PYTHON)
        add_test(
            NAME PythonTests
            COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(PythonTests PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python")
    endif()
endif()
