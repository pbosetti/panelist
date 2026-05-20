cmake_minimum_required(VERSION 3.20)

project(panelist VERSION 0.1.0 LANGUAGES CXX)

add_library(panelist INTERFACE)
add_library(panelist::panelist ALIAS panelist)

target_compile_features(panelist INTERFACE cxx_std_20)
target_include_directories(
  panelist
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

option(PANELIST_BUILD_TESTS "Build Panelist tests" ON)
option(PANELIST_BUILD_EXAMPLES "Build Panelist examples" ON)

if(PANELIST_BUILD_EXAMPLES)
  add_executable(panelist_three_panels examples/three_panels.cpp)
  target_link_libraries(panelist_three_panels PRIVATE panelist::panelist)
endif()

if(PANELIST_BUILD_TESTS)
  enable_testing()

  add_executable(panelist_compile_api tests/compile_api.cpp)
  target_link_libraries(panelist_compile_api PRIVATE panelist::panelist)

  add_test(NAME panelist_compile_api COMMAND panelist_compile_api)
endif()
