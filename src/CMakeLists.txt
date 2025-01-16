find_package(SQLite3 REQUIRED)
# find_package(Boost REQUIRED COMPONENTS algorithm)
set(HEADERS
    ../googlepinyinime-rev/src/include/atomdictbase.h
    ../googlepinyinime-rev/src/include/dictbuilder.h
    ../googlepinyinime-rev/src/include/dictdef.h
    ../googlepinyinime-rev/src/include/dictlist.h
    ../googlepinyinime-rev/src/include/dicttrie.h
    ../googlepinyinime-rev/src/include/lpicache.h
    ../googlepinyinime-rev/src/include/matrixsearch.h
    ../googlepinyinime-rev/src/include/mystdlib.h
    ../googlepinyinime-rev/src/include/ngram.h
    ../googlepinyinime-rev/src/include/pinyinime.h
    ../googlepinyinime-rev/src/include/searchutility.h
    ../googlepinyinime-rev/src/include/spellingtable.h
    ../googlepinyinime-rev/src/include/spellingtrie.h
    ../googlepinyinime-rev/src/include/splparser.h
    ../googlepinyinime-rev/src/include/sync.h
    ../googlepinyinime-rev/src/include/userdict.h
    ../googlepinyinime-rev/src/include/utf16char.h
    ../googlepinyinime-rev/src/include/utf16reader.h
)

set(SOURCES
    ../googlepinyinime-rev/src/share/dictbuilder.cpp
    ../googlepinyinime-rev/src/share/dictlist.cpp
    ../googlepinyinime-rev/src/share/dicttrie.cpp
    ../googlepinyinime-rev/src/share/lpicache.cpp
    ../googlepinyinime-rev/src/share/matrixsearch.cpp
    ../googlepinyinime-rev/src/share/mystdlib.cpp
    ../googlepinyinime-rev/src/share/ngram.cpp
    ../googlepinyinime-rev/src/share/pinyinime.cpp
    ../googlepinyinime-rev/src/share/searchutility.cpp
    ../googlepinyinime-rev/src/share/spellingtable.cpp
    ../googlepinyinime-rev/src/share/spellingtrie.cpp
    ../googlepinyinime-rev/src/share/splparser.cpp
    ../googlepinyinime-rev/src/share/sync.cpp
    ../googlepinyinime-rev/src/share/userdict.cpp
    ../googlepinyinime-rev/src/share/utf16char.cpp
    ../googlepinyinime-rev/src/share/utf16reader.cpp
    ./fanime.cpp
    ./dict.cpp
    ./log.cpp
    ./pinyin_utils.cpp
)

# Make sure it produce fanime.so instead of libfanime.so
add_library(fanime SHARED ${HEADERS} ${SOURCES})
target_link_libraries(fanime PRIVATE Fcitx5::Core Fcitx5::Module::QuickPhrase SQLite::SQLite3)
install(TARGETS fanime DESTINATION "${FCITX_INSTALL_LIBDIR}/fcitx5")

# Addon config file
# We need additional layer of conversion because we want PROJECT_VERSION in it.
configure_file(fanime-addon.conf.in.in fanime-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/fanime-addon.conf.in" fanime-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fanime-addon.conf" RENAME fanime.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")

# Input Method registration file
fcitx5_translate_desktop_file(fanime.conf.in fanime.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fanime.conf" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")
