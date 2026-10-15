install(DIRECTORY 48x48 scalable DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor"
        PATTERN .* EXCLUDE
        PATTERN *~ EXCLUDE)

# Frontend defaults belong to Bangke, not the distribution's shared Rime data.
# The lifecycle helper copies this into each user's Rime directory before deployment.
install(FILES fcitx5.yaml DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5-rime/seed")
