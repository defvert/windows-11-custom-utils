cmake_minimum_required(VERSION 4.3)
project(Win11Utils)

set(CMAKE_CXX_STANDARD 14)

add_executable(Win11Utils
        src/drop-tray/DropTray.cpp
        src/ethernet-restart/EthernetRestartUtility.cpp
        src/hover-inspector/HoverInspector.cpp
        src/icon-under-the-cursor/IconUnderTheCursor.cpp)
