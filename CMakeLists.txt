cmake_minimum_required(VERSION 2.8)
project(post2usenet)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME ON)

find_package(Boost REQUIRED COMPONENTS system filesystem program_options)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

set (CMAKE_CXX_FLAGS "-pthread -Wall -Wextra -Wno-missing-braces -std=c++11")
set (CMAKE_CXX_FLAGS_DEBUG "-g")
set (CMAKE_CXX_FLAGS_RELEASE "-O2 -Wl,-s -Wl,--gc-sections, -Wl,--print-gc-sections")

set (PROJECT_SOURCES
                     "./src/main.cc"
                     "./src/fileset.cc"
                     "./src/util/yencgenerator.cc"
                     "./src/yenc/yenc.cc"
                     "./src/program_config.cc"
                     "./src/nntp/connection.cc"
                     "./src/nntp/message.cc"
                     "./src/nntp/usenet.cc"
                     "./src/nntp/connection_info.cc")

add_executable(post2usenet ${PROJECT_SOURCES})
target_link_libraries(post2usenet ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

