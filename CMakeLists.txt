cmake_minimum_required(VERSION 3.10)
project(toy_web_server)

set(SRC
        main.cpp
        ./config/config.cpp
        ./webserver/webserver.cpp
        ./timer/lst_timer.cpp
        ./http/http_conn.cpp
        ./log/log.cpp
        ./CGImysql/sql_connection_pool.cpp
        )

add_executable(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} pthread libmysqlclient.so)