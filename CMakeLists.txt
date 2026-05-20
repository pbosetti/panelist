cmake_minimum_required(VERSION 3.20)

set(PANELIST_VERSION_TAG "v0.0.0")

find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --match
            "v[0-9]*.[0-9]*.[0-9]*" --abbrev=0
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE PANELIST_GIT_DESCRIBE_RESULT
    OUTPUT_VARIABLE PANELIST_GIT_DESCRIBE_OUTPUT
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(PANELIST_GIT_DESCRIBE_RESULT EQUAL 0
     AND PANELIST_GIT_DESCRIBE_OUTPUT MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+$")
    set(PANELIST_VERSION_TAG "${PANELIST_GIT_DESCRIBE_OUTPUT}")
  endif()
endif()

string(REGEX REPLACE "^v" "" PANELIST_PROJECT_VERSION
                     "${PANELIST_VERSION_TAG}")

project(panelist VERSION "${PANELIST_PROJECT_VERSION}" LANGUAGES CXX)

set(PANELIST_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(PANELIST_IS_TOP_LEVEL ON)
endif()

add_library(panelist INTERFACE)
add_library(panelist::panelist ALIAS panelist)

set(PANELIST_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/panelist_version.hpp.in"
  "${PANELIST_GENERATED_INCLUDE_DIR}/panelist/panelist_version.hpp" @ONLY)

target_compile_features(panelist INTERFACE cxx_std_17)
target_include_directories(
  panelist
  INTERFACE
    $<BUILD_INTERFACE:${PANELIST_GENERATED_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

option(PANELIST_BUILD_TESTS "Build Panelist tests" ${PANELIST_IS_TOP_LEVEL})
option(PANELIST_BUILD_EXAMPLES "Build Panelist examples" ${PANELIST_IS_TOP_LEVEL})
option(PANELIST_BUILD_DOCUMENTATION "Add the Panelist Doxygen documentation target"
       OFF)
option(PANELIST_BUILD_PACKAGE "Add the Panelist source package target"
       ${PANELIST_IS_TOP_LEVEL})

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

if(PANELIST_BUILD_DOCUMENTATION OR PANELIST_BUILD_PACKAGE)
  find_package(Doxygen QUIET)

  function(panelist_add_doxygen_target target_name)
    set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_NUMBER "${PANELIST_VERSION_TAG}")
    set(DOXYGEN_PROJECT_BRIEF "Header-only C++17 terminal panel output helper")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_HTML_OUTPUT html)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

    doxygen_add_docs(
      "${target_name}"
      "${CMAKE_CURRENT_SOURCE_DIR}/include/panelist/panelist.hpp"
      "${PANELIST_GENERATED_INCLUDE_DIR}/panelist/panelist_version.hpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
      COMMENT
        "Generate HTML documentation in ${CMAKE_CURRENT_BINARY_DIR}/docs/html")
  endfunction()
endif()

if(PANELIST_BUILD_DOCUMENTATION)
  if(DOXYGEN_FOUND)
    panelist_add_doxygen_target(document)
  else()
    add_custom_target(
      document
      COMMAND
        "${CMAKE_COMMAND}" -E echo
        "Doxygen was not found. Install Doxygen or configure with -DPANELIST_BUILD_DOCUMENTATION=OFF."
      COMMAND "${CMAKE_COMMAND}" -E false)
  endif()
endif()

if(PANELIST_BUILD_PACKAGE)
  if(DOXYGEN_FOUND)
    if(TARGET document)
      set(PANELIST_PACKAGE_DOCUMENTATION_TARGET document)
    else()
      panelist_add_doxygen_target(panelist_package_documentation)
      set(PANELIST_PACKAGE_DOCUMENTATION_TARGET panelist_package_documentation)
    endif()

    set(PANELIST_PACKAGE_NAME "${PROJECT_NAME}-${PANELIST_VERSION_TAG}")
    set(PANELIST_PACKAGE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/${PANELIST_PACKAGE_NAME}.tar.gz")

    add_custom_target(
      package
      COMMAND
        "${CMAKE_COMMAND}" "-DPANELIST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DPANELIST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DPANELIST_GENERATED_INCLUDE_DIR=${PANELIST_GENERATED_INCLUDE_DIR}"
        "-DPANELIST_PACKAGE_NAME=${PANELIST_PACKAGE_NAME}"
        "-DPANELIST_PACKAGE_FILE=${PANELIST_PACKAGE_FILE}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package.cmake"
      DEPENDS "${PANELIST_PACKAGE_DOCUMENTATION_TARGET}"
      BYPRODUCTS "${PANELIST_PACKAGE_FILE}"
      COMMENT "Create ${PANELIST_PACKAGE_FILE}"
      VERBATIM)
  else()
    add_custom_target(
      package
      COMMAND
        "${CMAKE_COMMAND}" -E echo
        "Doxygen was not found. Install Doxygen before building the package target."
      COMMAND "${CMAKE_COMMAND}" -E false)
  endif()
endif()
