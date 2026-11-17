cmake_minimum_required(VERSION 3.16)
project(singleproc_consumer LANGUAGES CXX)

find_package(singleproc 0.3 REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE singleproc::singleproc)
if(MSVC)
    # consumer 源文件含中文注释，显式 UTF-8；/W4 与主工程保持一致。
    target_compile_options(consumer PRIVATE /W4 /utf-8)
endif()
