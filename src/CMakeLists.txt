set(RIME_SOURCES
    rimestate.cpp
    rimeengine.cpp
    rimecandidate.cpp
    rimeservice.cpp
)
# addon 名是 rime-ai 而不是 rime：上游 fcitx5-rime 在发行版里就叫 rime，
# 同名会让两个包的 addon/rime.conf、inputmethod/rime.conf、rime.so 互相覆盖。
# 这个名字必须与三个地方一致：两个 conf 的文件名、Library=（addon conf）、
# profile 里注册的 Name=。
add_library(rime-ai MODULE ${RIME_SOURCES})
target_link_libraries(rime-ai Fcitx5::Core Fcitx5::Config PkgConfig::Rime Fcitx5::Module::Notifications Fcitx5::Module::DBus)
set_target_properties(rime-ai PROPERTIES PREFIX "")
install(TARGETS rime-ai DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(rime-ai.conf.in rime-ai.conf)
configure_file(rime-ai-addon.conf.in.in rime-ai-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/rime-ai-addon.conf.in" rime-ai-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rime-ai.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rime-ai-addon.conf" RENAME rime-ai.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")

