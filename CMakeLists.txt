cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/..")
set(VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed")
set(VCPKG_MANIFEST_INSTALL OFF)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake")

project(pieces)

include_directories(src)

add_executable(test
    test/main.cpp
)

find_package(stduuid CONFIG REQUIRED)
target_link_libraries(test PRIVATE stduuid)