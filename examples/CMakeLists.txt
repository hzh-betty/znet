cmake_minimum_required(VERSION 3.18)

# 示例程序
project(znet_examples LANGUAGES CXX)

# echo服务器示例
add_executable(echo_server echo_server.cc)
target_link_libraries(echo_server
    PRIVATE znet_shared
    PRIVATE zcoroutine_shared
    PRIVATE zlog_shared
)

# 简单HTTP服务器示例
add_executable(simple_http_server simple_http_server.cc)
target_link_libraries(simple_http_server
    PRIVATE znet_shared
    PRIVATE zcoroutine_shared
    PRIVATE zlog_shared
)

message(STATUS "==============================")
message(STATUS "znet examples configuration")
message(STATUS "------------------------------")
message(STATUS "Examples:")
message(STATUS "  - echo_server")
message(STATUS "  - simple_http_server")
message(STATUS "==============================")
