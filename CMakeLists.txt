cmake_minimum_required(VERSION 3.17)
project(MyCron C)

set(CMAKE_C_STANDARD 11)

add_executable(MyCron main.c log.c log.h)
SET(CMAKE_C_FLAGS -pthread)

find_library(LIBRT rt)
if(LIBRT)
    target_link_libraries(MyCron ${LIBRT})
endif()

