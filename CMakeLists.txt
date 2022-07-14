cmake_minimum_required(VERSION 3.16)

project (TINY_WEB_SERVER)

set(CMAKE_EXPORT_COMPILE_COMMANDS True)

include_directories(
    ./cgi-mysql
    ./config
    ./http
    ./lock
    ./log
    ./pch
    ./queue
    ./server
    ./thread
    ./timer
    ./utils
)

add_executable(TinyWebServer 
    main.cc 
    ./utils/utils.cc
    ./cgi-mysql/mysql_conn.cc
    ./config/config.cc
    ./http/http_conn.cc
    ./log/log.cc
    ./server/server.cc
    ./timer/timer.cc
)

target_link_libraries(TinyWebServer mysqlclient pthread)
