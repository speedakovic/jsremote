cmake_minimum_required(VERSION 2.8)
project(jsremote)

find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOLLER epoller REQUIRED)

set(JSREMOTE_SRC src/jsremote.cpp)
set(JSPEERTEST_SRC src/jspeertest.cpp src/jspeer.cpp)

include_directories(include ${EPOLLER_INCLUDE_DIRS})

add_executable(jsremote ${JSREMOTE_SRC})
target_link_libraries(jsremote ${EPOLLER_LIBRARIES})

add_executable(jspeertest ${JSPEERTEST_SRC})
target_link_libraries(jspeertest ${EPOLLER_LIBRARIES})

install(TARGETS jsremote DESTINATION bin)

