cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed")
set(VCPKG_MANIFEST_INSTALL OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
else()
    find_program(VCPKG_EXEC vcpkg)
    if(VCPKG_EXEC)
        get_filename_component(VCPKG_ROOT "${VCPKG_EXEC}" DIRECTORY)
    else()
        message(FATAL_ERROR "VCPKG_ROOT environment variable is not set and vcpkg executable not found in PATH. ")
    endif()
endif()
message(STATUS "Using vcpkg root: ${VCPKG_ROOT}")

if(VCPKG_TARGET_TRIPLET STREQUAL "wasm32-emscripten")
    if (NOT DEFINED ENV{EMSDK})
        message(FATAL_ERROR "EMSDK environment variable is not set. Please run emsdk_env script first.")
    endif()
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" CACHE PATH "Path to Emscripten toolchain" FORCE)
    message(STATUS "Using Emscripten toolchain: ${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
endif()

set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

project(pieces)

if(EMSCRIPTEN)
    add_subdirectory(wasm)
else()
    add_subdirectory(test)
endif()