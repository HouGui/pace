cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED PROJECT_VER)
  set(PROJECT_VER "0.1.0")
endif()
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(pace)

# ESP-IDF defaults to newer language modes; keep PACE's public contract explicit.
foreach(component pace_board pace_core pace_protocol pace_services pace_ui main)
  idf_component_get_property(component_target ${component} COMPONENT_LIB)
  target_compile_options(${component_target} PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-std=gnu17>"
    "$<$<COMPILE_LANGUAGE:CXX>:-std=c++17;-fno-exceptions;-fno-rtti>")
endforeach()
