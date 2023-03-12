#!/bin/bash

# Build nsblast using the build image, and then copy
# the binary and boost libraries to the artifacts location.

die() {
    echo "$*" 1>&2
    exit 1;
}

echo "Building nsblast..."

cd /build || die

cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} /src || die "CMake faild configure step"

make -j `nproc` || die "Build failed"

if ${DO_STRIP} ; then
    echo "Stripping binary"
    strip bin/*
    strip /usr/local/lib/libboost*
fi

cp -v bin/* /artifacts/bin
cp -rv /usr/local/lib/libboost* /artifacts/lib
#cp -v external-projects/installed/lib/*.so /artifacts/lib
