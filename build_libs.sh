#! /bin/bash -e

pushd vendor/mbedtls
mkdir -p build
cd build
cmake .. -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j 8
cp library/libmbed*.a ../../../libs/
popd


pushd vendor/libuv
mkdir -p build
cd build
cmake .. -DLIBUV_BUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j 8
cp libuv*.a ../../../libs/
popd

