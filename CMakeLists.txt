cmake_minimum_required(VERSION 3.17)
project(ThreadPool C)

set(CMAKE_C_STANDARD 99)
link_libraries(pthread)
add_executable(ThreadPool main.c ThreadPool.c ThreadPool.h)
