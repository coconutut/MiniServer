cmake_minimum_required(VERSION 3.10)

project(MiniServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(main
	main.cpp
	MiniServer.cpp
	ThreadPool/ThreadPool.cpp
	HttpConn/HttpConn.cpp
	SqlConnPool/SqlConnPool.cpp
	utils/utils.cpp
)

target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_library(MYSQLCLIENT_LIB NAMES mysqlclient mariadb)
if(NOT MYSQLCLIENT_LIB)
    message(FATAL_ERROR "MySQL client library not found. Install libmysqlclient-dev or libmariadb-dev.")
endif()

target_link_libraries(main PRIVATE ${MYSQLCLIENT_LIB})


find_package(OpenSSL REQUIRED)
target_link_libraries(main PRIVATE
    ${MYSQLCLIENT_LIB}
    OpenSSL::Crypto
)