cmake_minimum_required(VERSION 3.19)
project(TcpDeviceMonitorClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Sql Test)
qt_standard_project_setup()
add_subdirectory(../CommonProtocol ${CMAKE_BINARY_DIR}/CommonProtocol)

qt_add_executable(TcpDeviceMonitor WIN32
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/tcpclientworker.h src/tcpclientworker.cpp
    src/databaseworker.h src/databaseworker.cpp
    src/realtimeplot.h src/realtimeplot.cpp
    src/historydialog.h src/historydialog.cpp
)
target_link_libraries(TcpDeviceMonitor PRIVATE
    MonitorProtocol Qt6::Widgets Qt6::Network Qt6::Sql)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(DatabaseIntegrationTests
        tests/test_database.cpp
        src/databaseworker.h src/databaseworker.cpp
    )
    target_include_directories(DatabaseIntegrationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(DatabaseIntegrationTests PRIVATE MonitorProtocol Qt6::Sql Qt6::Test)
    add_test(NAME DatabaseIntegrationTests COMMAND DatabaseIntegrationTests -o database-results.txt,txt)
    qt_add_executable(WindowLifecycleTests tests/test_windowlifecycle.cpp
        src/mainwindow.h src/mainwindow.cpp
        src/tcpclientworker.h src/tcpclientworker.cpp
        src/databaseworker.h src/databaseworker.cpp
        src/realtimeplot.h src/realtimeplot.cpp
        src/historydialog.h src/historydialog.cpp)
    target_include_directories(WindowLifecycleTests PRIVATE src)
    target_link_libraries(WindowLifecycleTests PRIVATE MonitorProtocol Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Test)
    add_test(NAME WindowLifecycleTests COMMAND WindowLifecycleTests -platform offscreen -o lifecycle-results.txt,txt)
    set_tests_properties(WindowLifecycleTests PROPERTIES TIMEOUT 180)
endif()

install(TARGETS TcpDeviceMonitor RUNTIME DESTINATION .)
