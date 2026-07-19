cmake_minimum_required(VERSION 3.12)

project(oclip LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED true)

add_executable(oclip src/oclip.c)

install(TARGETS oclip)
