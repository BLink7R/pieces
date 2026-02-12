find_package(stduuid CONFIG REQUIRED)
find_package(utf8cpp CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_executable(random_test
    randomtest.cpp
    ../src/text.cpp
)
target_include_directories(random_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(random_test PRIVATE GTest::gtest GTest::gtest_main stduuid utf8cpp::utf8cpp)

add_executable(unit_test
    unittest.cpp
    ../src/text.cpp
)
target_include_directories(unit_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(unit_test PRIVATE GTest::gtest GTest::gtest_main stduuid utf8cpp::utf8cpp)

enable_testing()
add_test(NAME random_test COMMAND random_test)
add_test(NAME unit_test COMMAND unit_test)
