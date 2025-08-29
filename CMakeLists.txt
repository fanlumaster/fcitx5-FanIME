cmake_minimum_required(VERSION 3.27)
project(fcitx5-fanime VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")

# 设置 Boost 的根目录
set(BOOST_ROOT "/opt/homebrew/opt/boost")

# 添加 Boost 头文件路径
include_directories("/opt/homebrew/opt/boost/include")
include_directories("/Library/Input Methods/Fcitx5.app/Contents/include/Fcitx5/Module/fcitx-module/fcitx5-chinese-addons/modules")

if (FAN_DEBUG)
  add_definitions(-DFAN_DEBUG)
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET 13) # Also need to update CGO_CFLAGS

    set(APP_CONTENTS_PATH "/Library/Input Methods/Fcitx5.app/Contents")
    set(CMAKE_FIND_ROOT_PATH
        "${APP_CONTENTS_PATH}"
        "${FIND_ROOT_PATH}"
    )

find_package(Gettext REQUIRED)
find_package(Fcitx5Core REQUIRED)
# find_package(Fcitx5Module REQUIRED COMPONENTS Punctuation QuickPhrase)
find_package(Fcitx5Module REQUIRED COMPONENTS QuickPhrase)

# Setup some compiler option that is generally useful and compatible with Fcitx 5 (C++17)
# include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

add_subdirectory(src)
add_subdirectory(po)
