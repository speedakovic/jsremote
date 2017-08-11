cmake_minimum_required(VERSION 2.8)
project(jsremote)

find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOLLER epoller REQUIRED)

set(SOURCES src/jsremote.cpp)

include_directories(include ${EPOLLER_INCLUDE_DIRS})
add_executable(jsremote ${SOURCES})
target_link_libraries(jsremote ${EPOLLER_LIBRARIES})

install(TARGETS jsremote DESTINATION bin)

