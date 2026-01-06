cmake_minimum_required(VERSION 3.18)

project(znet_project LANGUAGES CXX)

# 设置C++14标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(DCMAKE_EXPORT_COMPILE_COMMANDS ON)
# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-function -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

# Debug和Release配置
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -ggdb")
    message(STATUS "Build type: Debug with full debug symbols")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -DNDEBUG")
    message(STATUS "Build type: Release with optimizations")
endif()

# 输出目录配置（输出到build目录下）
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

# 查找依赖
find_package(Threads REQUIRED)
find_package(fmt REQUIRED CONFIG)

# 启用测试支持
enable_testing()

# 添加子项目
message(STATUS "==============================")
message(STATUS "Building znet project")
message(STATUS "------------------------------")
message(STATUS "C++ Standard      : ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "==============================")

# 构建顺序：zlog -> zcoroutine -> znet
add_subdirectory(zlog)
add_subdirectory(zcoroutine)
add_subdirectory(znet)

message(STATUS "All subprojects configured successfully")
