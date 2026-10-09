cmake_minimum_required(VERSION 3.6.0)
project(fcitx5-rime-config VERSION 5.0.11 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets DBus Network Gui Core)

add_executable(fcitx5-rime-config
    src/fcitxprofile.cpp
    src/fcitxprofile.h
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/yamlconfig.cpp
    src/yamlconfig.h
    src/rimedeploy.cpp
    src/rimedeploy.h
    src/theme/color.cpp
    src/theme/color.h
    src/theme/inifile.cpp
    src/theme/migrate.cpp
    src/theme/inifile.h
    src/theme/native.h
    src/theme/ninegrid.cpp
    src/theme/ninegrid.h
    src/theme/panelspec.cpp
    src/theme/panelspec.h
    src/theme/paths.cpp
    src/theme/paths.h
    src/theme/scheme.cpp
    src/theme/scheme.h
    src/theme/stylepatch.cpp
    src/theme/stylepatch.h
    src/theme/themewriter.cpp
    src/theme/themewriter.h
    src/ui/fontpicker.cpp
    src/ui/fontpicker.h
    src/ui/pages.cpp
    src/ui/pages.h
    src/ui/preview.cpp
    src/ui/preview.h
    src/ui/themepalette.cpp
    src/ui/themepalette.h
    src/ui/widgets.cpp
    src/ui/widgets.h
)

target_include_directories(fcitx5-rime-config PRIVATE src)
target_link_libraries(fcitx5-rime-config PRIVATE Qt5::Widgets Qt5::DBus Qt5::Network)

install(TARGETS fcitx5-rime-config DESTINATION bin)
install(FILES data/fcitx5-rime-config.desktop DESTINATION share/applications)

# 补丁写盘的回归测试（不随包分发）。
add_executable(stylepatch_test EXCLUDE_FROM_ALL
    tests/stylepatch_test.cpp
    src/theme/color.cpp
    src/theme/paths.cpp
    src/theme/scheme.cpp
    src/theme/stylepatch.cpp
)
target_include_directories(stylepatch_test PRIVATE src)
target_link_libraries(stylepatch_test PRIVATE Qt5::Gui)

# Switch 自绘控件的回归测试（不随包分发）。
# Qt5Test 在打包用的 22.04 容器里没有 —— 拿不到就跳过，别挡着出包。
find_package(Qt5Test QUIET)
if (TARGET Qt5::Test)
    add_executable(switch_test EXCLUDE_FROM_ALL
        tests/switch_test.cpp
        src/ui/widgets.cpp
        src/ui/themepalette.cpp
    )
    target_include_directories(switch_test PRIVATE src)
    target_link_libraries(switch_test PRIVATE Qt5::Widgets Qt5::Test)

    # 真实页面里的开关（复现用户机器上的问题）
    add_executable(page_test EXCLUDE_FROM_ALL
        tests/page_test.cpp
        src/theme/color.cpp src/theme/inifile.cpp src/theme/ninegrid.cpp
        src/theme/panelspec.cpp src/theme/paths.cpp src/theme/scheme.cpp
        src/theme/stylepatch.cpp src/theme/themewriter.cpp
        src/ui/fontpicker.cpp src/ui/pages.cpp src/ui/preview.cpp
        src/ui/themepalette.cpp src/ui/widgets.cpp
    )
    target_include_directories(page_test PRIVATE src)
    target_link_libraries(page_test PRIVATE Qt5::Widgets Qt5::Test)

endif ()

# 「把配置推给运行中的插件」的验证（需要会话总线；不依赖 Qt5Test）。
add_executable(addonconfig_test EXCLUDE_FROM_ALL
    tests/addonconfig_test.cpp
    src/rimedeploy.cpp
)
target_include_directories(addonconfig_test PRIVATE src)
target_link_libraries(addonconfig_test PRIVATE Qt5::DBus)

# 输入法注册的回归测试。
add_executable(profile_test EXCLUDE_FROM_ALL
    tests/profile_test.cpp
    src/fcitxprofile.cpp
)
target_include_directories(profile_test PRIVATE src)
target_link_libraries(profile_test PRIVATE Qt5::Core)
