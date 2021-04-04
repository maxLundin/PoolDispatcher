cmake_minimum_required(VERSION 3.3)
project(jb_pool)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(lib)

include_directories(lib)

add_executable(jb_pool main.cpp)

add_executable(jb_pool_test tests/TestTaskQueue.cpp)

target_link_libraries(jb_pool pool_lib -lpthread)
target_link_libraries(jb_pool_test pool_lib -lgtest -lpthread)