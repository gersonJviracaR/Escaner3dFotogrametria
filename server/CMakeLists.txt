cmake_minimum_required(VERSION 3.21)
project(escaner3d_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Ceres CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# Núcleo reutilizable por los Sprints 2-4.
add_library(scanner_core STATIC
  src/quality.cpp
)
target_include_directories(scanner_core PUBLIC src)
target_link_libraries(scanner_core PUBLIC opencv_core opencv_imgproc opencv_imgcodecs)

enable_testing()
add_executable(scanner_tests
  tests/test_quality.cpp
)
target_link_libraries(scanner_tests PRIVATE scanner_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(scanner_tests DISCOVERY_MODE PRE_TEST)

# Spike SP-0: verifica que OpenCV y Ceres compilan y enlazan.
add_executable(spike_main spike/spike_main.cpp)
target_link_libraries(spike_main PRIVATE opencv_core opencv_imgproc opencv_imgcodecs Ceres::ceres)
