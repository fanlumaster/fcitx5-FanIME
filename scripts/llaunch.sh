# release version
# mkdir -p build
# cd build
# cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug
# make
# sudo make install
# cd ..
cp ./build/macos-arm64/src/libfanime.so ~/Library/fcitx5/lib/fcitx5
