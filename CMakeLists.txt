cmake_minimum_required(VERSION 2.8)
project(jb_pool)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wshadow=compatible-local")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -mtune=native")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")


#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")

add_subdirectory(lib)

include_directories(lib)

add_executable(jb_pool main.cpp)

add_executable(jb_pool_test tests/TestTaskQueue.cpp)

target_link_libraries(jb_pool pool_lib -lpthread)
target_link_libraries(jb_pool_test pool_lib -lgtest -lpthread)
