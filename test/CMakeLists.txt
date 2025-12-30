
add_executable(test main.cpp)

target_include_directories(test PRIVATE ${CMAKE_SOURCE_DIR}/src)

find_package(stduuid CONFIG REQUIRED)
find_package(utf8cpp CONFIG REQUIRED)
target_link_libraries(test PRIVATE stduuid utf8cpp::utf8cpp)
