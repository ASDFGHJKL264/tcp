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
    src/historyqueryworker.h src/historyqueryworker.cpp
    src/asyncfilelogger.h src/asyncfilelogger.cpp
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
        src/historydialog.h src/historydialog.cpp
        src/historyqueryworker.h src/historyqueryworker.cpp
        src/asyncfilelogger.h src/asyncfilelogger.cpp)
    target_include_directories(WindowLifecycleTests PRIVATE src)
    target_link_libraries(WindowLifecycleTests PRIVATE MonitorProtocol Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Test)
    add_test(NAME WindowLifecycleTests COMMAND WindowLifecycleTests -platform offscreen -o lifecycle-results.txt,txt)
    set_tests_properties(WindowLifecycleTests PROPERTIES TIMEOUT 180)
    qt_add_executable(HistoryQueryTests tests/test_history.cpp
        src/historydialog.h src/historydialog.cpp
        src/historyqueryworker.h src/historyqueryworker.cpp)
    target_include_directories(HistoryQueryTests PRIVATE src)
    target_link_libraries(HistoryQueryTests PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test)
    add_test(NAME HistoryQueryTests COMMAND HistoryQueryTests -platform offscreen -o history-results.txt,txt)
    set_tests_properties(HistoryQueryTests PROPERTIES TIMEOUT 45)
    qt_add_executable(PresentationTests tests/test_presentation.cpp
        src/asyncfilelogger.h src/asyncfilelogger.cpp
        src/realtimeplot.h src/realtimeplot.cpp)
    target_include_directories(PresentationTests PRIVATE src)
    target_link_libraries(PresentationTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME PresentationTests COMMAND PresentationTests -platform offscreen -o presentation-results.txt,txt)
    set_tests_properties(PresentationTests PROPERTIES TIMEOUT 30)
endif()

install(TARGETS TcpDeviceMonitor RUNTIME DESTINATION .)
