cmake_minimum_required(VERSION 3.10)
project(tinybma)

add_executable(${CMAKE_PROJECT_NAME} tinybma.cpp)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/external/)