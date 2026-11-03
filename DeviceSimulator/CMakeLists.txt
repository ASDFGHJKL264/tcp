cmake_minimum_required(VERSION 3.19)
project(DeviceSimulatorProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)
qt_standard_project_setup()
add_subdirectory(../CommonProtocol ${CMAKE_BINARY_DIR}/CommonProtocol)

qt_add_executable(DeviceSimulator WIN32
    simulator/simulatormain.cpp
    simulator/simulatorwindow.h simulator/simulatorwindow.cpp
    simulator/simulatorserver.h simulator/simulatorserver.cpp
)
target_link_libraries(DeviceSimulator PRIVATE
    MonitorProtocol Qt6::Widgets Qt6::Network)

install(TARGETS DeviceSimulator RUNTIME DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(SimulatorUiTests tests/test_simulatorui.cpp
        simulator/simulatorwindow.h simulator/simulatorwindow.cpp
        simulator/simulatorserver.h simulator/simulatorserver.cpp)
    target_include_directories(SimulatorUiTests PRIVATE simulator)
    target_link_libraries(SimulatorUiTests PRIVATE MonitorProtocol Qt6::Widgets Qt6::Network Qt6::Test)
    add_test(NAME SimulatorUiTests COMMAND SimulatorUiTests -platform offscreen -o simulator-ui-results.txt,txt)
    set_tests_properties(SimulatorUiTests PROPERTIES TIMEOUT 30)
endif()
