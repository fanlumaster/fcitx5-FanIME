# debug version
mkdir -p build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug -DFAN_DEBUG=1
make
sudo make install
cd ..
