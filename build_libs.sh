#! /bin/bash -e

CMAKE_OPTS="-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64"

pushd vendor/mbedtls
mkdir -p build
cd build
cmake .. $CMAKE_OPTS -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j 8
cp library/libmbed*.a ../../../libs/
popd


pushd vendor/libuv
mkdir -p build
cd build
cmake .. $CMAKE_OPTS -DLIBUV_BUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j 8
cp libuv*.a ../../../libs/
popd

