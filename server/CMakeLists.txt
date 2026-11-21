cmake_minimum_required(VERSION 3.21)
project(escaner3d_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs features2d calib3d)
find_package(Ceres CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Núcleo reutilizable por los Sprints 2-4.
add_library(scanner_core STATIC
  src/atomic_file.cpp
  src/config.cpp
  src/http_server.cpp
  src/manifest.cpp
  src/matching_params.cpp
  src/quality.cpp
  src/session_store.cpp
)
target_include_directories(scanner_core PUBLIC src)
target_link_libraries(scanner_core PUBLIC
  opencv_core opencv_imgproc opencv_imgcodecs opencv_features2d opencv_calib3d
  httplib::httplib
  nlohmann_json::nlohmann_json
)

add_executable(scanner_server src/main.cpp)
target_link_libraries(scanner_server PRIVATE scanner_core)
add_custom_command(TARGET scanner_server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/config.default.json $<TARGET_FILE_DIR:scanner_server>/config.default.json)

enable_testing()
add_executable(scanner_tests
  tests/test_config.cpp
  tests/test_http_integration.cpp
  tests/test_manifest.cpp
  tests/test_quality.cpp
  tests/test_session_store.cpp
)
target_link_libraries(scanner_tests PRIVATE scanner_core GTest::gtest_main)
target_compile_definitions(scanner_tests PRIVATE
  ESCANER3D_DEFAULT_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config.default.json")
include(GoogleTest)
gtest_discover_tests(scanner_tests DISCOVERY_MODE PRE_TEST)

# Spike SP-0: verifica que OpenCV y Ceres compilan y enlazan.
add_executable(spike_main spike/spike_main.cpp)
target_link_libraries(spike_main PRIVATE opencv_core opencv_imgproc opencv_imgcodecs Ceres::ceres)
