set(RIME_SOURCES
    rimestate.cpp
    rimeengine.cpp
    rimecandidate.cpp
    rimeservice.cpp
    rimepanel.cpp
)
# addon 名是 rime-ai 而不是 rime：上游 fcitx5-rime 在发行版里就叫 rime，
# 同名会让两个包的 addon/rime.conf、inputmethod/rime.conf、rime.so 互相覆盖。
# 这个名字必须与三个地方一致：两个 conf 的文件名、Library=（addon conf）、
# profile 里注册的 Name= —— 所以这里只写一处，其余都由它推出来。
set(RIME_ADDON_NAME rime-ai)

add_library(${RIME_ADDON_NAME} MODULE ${RIME_SOURCES})
if(BANGKE_PRIVATE_PANEL)
  target_compile_definitions(${RIME_ADDON_NAME} PRIVATE BANGKE_PRIVATE_PANEL)
  if(Fcitx5Core_VERSION VERSION_GREATER_EQUAL "5.1.22")
    target_compile_definitions(${RIME_ADDON_NAME} PRIVATE BANGKE_OVERLAY_MESSAGE)
  endif()
endif()
target_link_libraries(${RIME_ADDON_NAME} Fcitx5::Core Fcitx5::Config Fcitx5::Module::Notifications Fcitx5::Module::DBus)
if(BANGKE_ISOLATED_RUNTIME)
  target_include_directories(${RIME_ADDON_NAME} PRIVATE ${Rime_INCLUDE_DIRS})
  target_link_libraries(${RIME_ADDON_NAME} ${BANGKE_RIME_LIBRARY})
  target_compile_definitions(${RIME_ADDON_NAME} PRIVATE rime_get_api=bangke_rime_get_api)
  set_target_properties(${RIME_ADDON_NAME} PROPERTIES INSTALL_RPATH "/usr/lib/fcitx5-rime-ai")
else()
  target_link_libraries(${RIME_ADDON_NAME} PkgConfig::Rime)
endif()
set_target_properties(${RIME_ADDON_NAME} PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
# 代码里要按自己的 addon 名去 input method 表里找注册条目（见 rimeengine.cpp 的
# displayName）；5.0.x 的 fcitx5 还没有 AddonInstance::addonInfo()，只能这样带进去。
target_compile_definitions(${RIME_ADDON_NAME} PRIVATE FCITX_RIME_ADDON_NAME="${RIME_ADDON_NAME}")
install(TARGETS ${RIME_ADDON_NAME} DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(${RIME_ADDON_NAME}.conf.in ${RIME_ADDON_NAME}.conf)
configure_file(${RIME_ADDON_NAME}-addon.conf.in.in ${RIME_ADDON_NAME}-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/${RIME_ADDON_NAME}-addon.conf.in" ${RIME_ADDON_NAME}-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${RIME_ADDON_NAME}.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${RIME_ADDON_NAME}-addon.conf" RENAME ${RIME_ADDON_NAME}.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
