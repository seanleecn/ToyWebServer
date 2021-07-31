cmake_minimum_required(VERSION 3.10)
project(toy_web_server)

set(SRC
        main.cpp
        connpool/conn_pool.cpp
        ./http/http_conn.cpp
        ./log/log.cpp
        ./webserver/webserver.cpp
        ./timer/timer.cpp   
)

add_executable(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} pthread libmysqlclient.so)