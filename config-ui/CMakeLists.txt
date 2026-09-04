cmake_minimum_required(VERSION 3.6.0)
project(fcitx5-rime-config VERSION 5.0.11 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets DBus Network)

add_executable(fcitx5-rime-config
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/yamlconfig.cpp
    src/yamlconfig.h
    src/rimedeploy.cpp
    src/rimedeploy.h
)

target_link_libraries(fcitx5-rime-config PRIVATE Qt5::Widgets Qt5::DBus Qt5::Network)

install(TARGETS fcitx5-rime-config DESTINATION bin)
install(FILES data/fcitx5-rime-config.desktop DESTINATION share/applications)
