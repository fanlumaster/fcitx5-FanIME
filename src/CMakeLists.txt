find_package(SQLite3 REQUIRED)
# find_package(Boost REQUIRED COMPONENTS algorithm)

# Make sure it produce fanime.so instead of libfanime.so
add_library(fanime SHARED fanime.cpp dict.cpp log.cpp utils.cpp)
target_link_libraries(fanime PRIVATE Fcitx5::Core Fcitx5::Module::Punctuation  Fcitx5::Module::QuickPhrase SQLite::SQLite3)
install(TARGETS fanime DESTINATION "${FCITX_INSTALL_LIBDIR}/fcitx5")

# Addon config file
# We need additional layer of conversion because we want PROJECT_VERSION in it.
configure_file(fanime-addon.conf.in.in fanime-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/fanime-addon.conf.in" fanime-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fanime-addon.conf" RENAME fanime.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")

# Input Method registration file
fcitx5_translate_desktop_file(fanime.conf.in fanime.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fanime.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")
