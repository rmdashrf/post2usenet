cmake_minimum_required(VERSION 2.8)
project(post2usenet)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(Boost 1.57.0 REQUIRED COMPONENTS system coroutine filesystem program_options)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

set (CMAKE_CXX_FLAGS "-g -pthread -Wall -std=c++11")

set (PROJECT_SOURCES "./src/main.cc"
                     "./src/nntp/connection.cc"
                     "./src/nntp/message.cc"
                     "./src/nntp/usenet.cc"
                     "./src/nntp/connection_info.cc")

add_executable(post2usenet ${PROJECT_SOURCES})
target_link_libraries(post2usenet ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

