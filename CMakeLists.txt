cmake_minimum_required(VERSION 3.20)

project(panelist VERSION 0.1.0 LANGUAGES CXX)

set(PANELIST_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(PANELIST_IS_TOP_LEVEL ON)
endif()

add_library(panelist INTERFACE)
add_library(panelist::panelist ALIAS panelist)

target_compile_features(panelist INTERFACE cxx_std_17)
target_include_directories(
  panelist
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

option(PANELIST_BUILD_TESTS "Build Panelist tests" ${PANELIST_IS_TOP_LEVEL})
option(PANELIST_BUILD_EXAMPLES "Build Panelist examples" ${PANELIST_IS_TOP_LEVEL})
option(PANELIST_BUILD_DOCUMENTATION "Add the Panelist Doxygen documentation target"
       OFF)

if(PANELIST_BUILD_EXAMPLES)
  add_executable(panelist_three_panels examples/three_panels.cpp)
  target_link_libraries(panelist_three_panels PRIVATE panelist::panelist)
endif()

if(PANELIST_BUILD_TESTS)
  enable_testing()

  add_executable(panelist_compile_api tests/compile_api.cpp)
  target_link_libraries(panelist_compile_api PRIVATE panelist::panelist)

  add_test(NAME panelist_compile_api COMMAND panelist_compile_api)

  set(PANELIST_FETCH_CONTENT_BINARY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/fetch_content_consumer")
  add_test(
    NAME panelist_fetch_content_configure
    COMMAND
      "${CMAKE_COMMAND}" -S
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fetch_content_consumer" -B
      "${PANELIST_FETCH_CONTENT_BINARY_DIR}" -G "${CMAKE_GENERATOR}")
  add_test(
    NAME panelist_fetch_content_build
    COMMAND "${CMAKE_COMMAND}" --build
            "${PANELIST_FETCH_CONTENT_BINARY_DIR}")
  set_tests_properties(panelist_fetch_content_build
                       PROPERTIES DEPENDS panelist_fetch_content_configure)
endif()

if(PANELIST_BUILD_DOCUMENTATION)
  find_package(Doxygen QUIET)

  if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_BRIEF
        "Header-only C++17 terminal panel output helper")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_HTML_OUTPUT html)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

    doxygen_add_docs(
      document
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
      COMMENT
        "Generate HTML documentation in ${CMAKE_CURRENT_BINARY_DIR}/docs/html")
  else()
    add_custom_target(
      document
      COMMAND
        "${CMAKE_COMMAND}" -E echo
        "Doxygen was not found. Install Doxygen or configure with -DPANELIST_BUILD_DOCUMENTATION=OFF."
      COMMAND "${CMAKE_COMMAND}" -E false)
  endif()
endif()
