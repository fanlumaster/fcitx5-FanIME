# debug version
mkdir -p build
cd build
mkdir -p macos-arm64
cd macos-arm64
cmake ../..
cd ../..
CGO_CFLAGS="-mmacosx-version-min=13" GOFLAGS="-ldflags=-buildid=" cmake --build build/macos-arm64