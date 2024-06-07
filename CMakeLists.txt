cmake_minimum_required(VERSION 3.1.0)
project(memory_pool)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall ")
set(CMAKE_BUILD_TYPE Debug)

include_directories(${CMAKE_SOURCE_DIR}/include)
file(GLOB SRC_FILES    #注意这里定义都shell 变量 SRC_FILES 一定要对应在add_executable中!
        "${PROJECT_SOURCE_DIR}/*.cpp"
)
file(GLOB HEAD_FILES
        "${PROJECT_SOURCE_DIR}/*.h"
)

#查找依赖库
find_package(Threads REQUIRED)

add_executable(${CMAKE_PROJECT_NAME}
        ${SRC_FILES}
        ${HEAD_FILES}
        ConcurrentAlloc.cpp
        #        ObjectPool.h
#        benchmark.cpp
)
#引入依赖库
target_link_libraries(${CMAKE_PROJECT_NAME} Threads::Threads)



