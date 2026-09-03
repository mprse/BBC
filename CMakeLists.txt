cmake_minimum_required(VERSION 3.25)

project(
    BBC
    VERSION 0.1.0
    DESCRIPTION "An educational blockchain project"
    LANGUAGES CXX
)

add_executable(bbc src/main.cpp)

target_compile_features(bbc PRIVATE cxx_std_20)
set_target_properties(bbc PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(bbc PRIVATE /W4 /permissive-)
else()
    target_compile_options(bbc PRIVATE -Wall -Wextra -Wpedantic)
endif()
