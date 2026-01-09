cmake_minimum_required(VERSION 3.15)
project(CheckDeps)

message(STATUS "--- Checking System Dependencies ---")

# 1. OpenCV
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    message(STATUS "[OK] OpenCV found: ${OpenCV_VERSION}")
else()
    message(STATUS "[MISSING] OpenCV not found")
endif()

# 2. TBB
find_package(TBB QUIET)
if(TBB_FOUND)
    message(STATUS "[OK] TBB found: ${TBB_VERSION}")
else()
    # Try searching via PkgConfig as fallback
    find_package(PkgConfig QUIET)
    pkg_check_modules(TBB_PKG tbb)
    if(TBB_PKG_FOUND)
        message(STATUS "[OK] TBB found via PkgConfig: ${TBB_PKG_VERSION}")
    else()
        message(STATUS "[MISSING] TBB not found")
    endif()
endif()

# 3. spdlog
find_package(spdlog QUIET)
if(spdlog_FOUND)
    message(STATUS "[OK] spdlog found: ${spdlog_VERSION}")
else()
    message(STATUS "[MISSING] spdlog not found")
endif()

message(STATUS "------------------------------------")
