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

# 2. spdlog
find_package(spdlog QUIET)
if(spdlog_FOUND)
    message(STATUS "[OK] spdlog found: ${spdlog_VERSION}")
else()
    message(STATUS "[MISSING] spdlog not found")
endif()

message(STATUS "------------------------------------")
