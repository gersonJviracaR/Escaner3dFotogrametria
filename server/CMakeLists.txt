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
  src/bundle_adjustment.cpp
  src/camera_model.cpp
  src/colmap_io.cpp
  src/colmap_runner.cpp
  src/config.cpp
  src/dense.cpp
  src/epipolar.cpp
  src/features.cpp
  src/http_server.cpp
  src/initialization.cpp
  src/intrinsics.cpp
  src/manifest.cpp
  src/matching.cpp
  src/meshing.cpp
  src/matching_params.cpp
  src/matching_report.cpp
  src/pairing.cpp
  src/pipeline_params.cpp
  src/quality.cpp
  src/reconstruction.cpp
  src/reconstruction_io.cpp
  src/reconstruction_params.cpp
  src/reconstruction_report.cpp
  src/registration.cpp
  src/session_matching.cpp
  src/session_pipeline.cpp
  src/session_reconstruction.cpp
  src/session_store.cpp
  src/sfm_types.cpp
  src/tracks.cpp
  src/triangulation.cpp
  src/visibility.cpp
)
target_include_directories(scanner_core PUBLIC src)
target_link_libraries(scanner_core PUBLIC
  opencv_core opencv_imgproc opencv_imgcodecs opencv_features2d opencv_calib3d
  httplib::httplib
  nlohmann_json::nlohmann_json
  Ceres::ceres
)

add_executable(scanner_server src/main.cpp)
target_link_libraries(scanner_server PRIVATE scanner_core)
add_custom_command(TARGET scanner_server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/config.default.json $<TARGET_FILE_DIR:scanner_server>/config.default.json)

# Sprint 2: procesa una sesión ya almacenada (características, pares, F/E).
add_executable(scanner_match tools/scanner_match.cpp)
target_link_libraries(scanner_match PRIVATE scanner_core)

# Sprint 3: reconstrucción dispersa y bundle adjustment de una sesión ya emparejada.
add_executable(scanner_reconstruct tools/scanner_reconstruct.cpp)
target_link_libraries(scanner_reconstruct PRIVATE scanner_core)
add_custom_command(TARGET scanner_reconstruct POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/config.default.json $<TARGET_FILE_DIR:scanner_reconstruct>/config.default.json)

# Sprint 4: herramientas del Epic 4 (copian config.default.json junto al ejecutable).
foreach(tool scanner_dense)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE scanner_core)
  add_custom_command(TARGET ${tool} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config.default.json $<TARGET_FILE_DIR:${tool}>/config.default.json)
endforeach()

enable_testing()
add_executable(scanner_tests
  tests/test_bundle_adjustment.cpp
  tests/test_camera_model.cpp
  tests/test_colmap.cpp
  tests/test_config.cpp
  tests/test_epipolar.cpp
  tests/test_features.cpp
  tests/test_http_integration.cpp
  tests/test_initialization.cpp
  tests/test_intrinsics.cpp
  tests/test_manifest.cpp
  tests/test_matching.cpp
  tests/test_meshing.cpp
  tests/test_matching_report.cpp
  tests/test_pairing.cpp
  tests/test_quality.cpp
  tests/test_reconstruction_io.cpp
  tests/test_reconstruction_report.cpp
  tests/test_registration.cpp
  tests/test_session_matching.cpp
  tests/test_session_pipeline.cpp
  tests/test_session_reconstruction.cpp
  tests/test_session_store.cpp
  tests/test_tracks.cpp
  tests/test_triangulation.cpp
  tests/test_visibility.cpp
)
# Ejecutable falso de COLMAP para probar el ejecutor de procesos (Sprint 4).
add_executable(colmap_stub tests/colmap_stub.cpp)

target_link_libraries(scanner_tests PRIVATE scanner_core GTest::gtest_main)
add_dependencies(scanner_tests colmap_stub)
target_compile_definitions(scanner_tests PRIVATE
  ESCANER3D_DEFAULT_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config.default.json"
  COLMAP_STUB_PATH="$<TARGET_FILE:colmap_stub>")
include(GoogleTest)
gtest_discover_tests(scanner_tests DISCOVERY_MODE PRE_TEST)

# Spike SP-0: verifica que OpenCV y Ceres compilan y enlazan.
add_executable(spike_main spike/spike_main.cpp)
target_link_libraries(spike_main PRIVATE opencv_core opencv_imgproc opencv_imgcodecs Ceres::ceres)
