cmake_minimum_required(VERSION 3.19)
project(TcpMonitorSystemIntegration LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Test)
qt_standard_project_setup()
add_subdirectory(CommonProtocol)

include(CTest)
qt_add_executable(NetworkIntegrationTests
    tests/test_networkintegration.cpp
    TcpDeviceMonitorClient/src/tcpclientworker.h
    TcpDeviceMonitorClient/src/tcpclientworker.cpp
    DeviceSimulator/simulator/simulatorserver.h
    DeviceSimulator/simulator/simulatorserver.cpp
)
target_include_directories(NetworkIntegrationTests PRIVATE
    TcpDeviceMonitorClient/src DeviceSimulator/simulator)
target_link_libraries(NetworkIntegrationTests PRIVATE MonitorProtocol Qt6::Network Qt6::Test)
add_test(NAME NetworkIntegrationTests COMMAND NetworkIntegrationTests -o network-results.txt,txt)

qt_add_executable(ReliabilityTests tests/test_reliability.cpp
    TcpDeviceMonitorClient/src/tcpclientworker.h
    TcpDeviceMonitorClient/src/tcpclientworker.cpp
    DeviceSimulator/simulator/simulatorserver.h
    DeviceSimulator/simulator/simulatorserver.cpp)
target_include_directories(ReliabilityTests PRIVATE TcpDeviceMonitorClient/src DeviceSimulator/simulator)
target_link_libraries(ReliabilityTests PRIVATE MonitorProtocol Qt6::Network Qt6::Test)
add_test(NAME ReliabilityTests COMMAND ReliabilityTests -o reliability-results.txt,txt)
set_tests_properties(ReliabilityTests PROPERTIES TIMEOUT 90)
