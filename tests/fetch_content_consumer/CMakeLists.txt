cmake_minimum_required(VERSION 3.20)

project(panelist_fetch_content_consumer LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(panelist SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
FetchContent_MakeAvailable(panelist)

add_executable(fetch_content_consumer main.cpp)
target_link_libraries(fetch_content_consumer PRIVATE panelist::panelist)
