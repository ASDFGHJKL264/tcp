cmake_minimum_required(VERSION 3.19)
project(CommonProtocol VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

add_library(MonitorProtocol STATIC
    protocol.h
    protocol.cpp
)
target_include_directories(MonitorProtocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MonitorProtocol PUBLIC Qt6::Core)

if(PROJECT_IS_TOP_LEVEL)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    include(CTest)
    qt_add_executable(MonitorProtocolTests tests/test_protocol.cpp)
    target_link_libraries(MonitorProtocolTests PRIVATE MonitorProtocol Qt6::Test)
    add_test(NAME MonitorProtocolTests COMMAND MonitorProtocolTests)
endif()
