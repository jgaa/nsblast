#!/bin/bash

# Build nsblast using the build image, and then copy
# the binary and boost to the artifacts location.

die() {
    echo "$*" 1>&2
    exit 1;
}

echo "Building nsblast..."

cd /build || die

cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DNSBLAST_RUN_TESTS=${NSBLAST_RUN_TESTS} \
      -DNSBLAST_WITH_SWAGGER=${WITH_SWAGGER} /src \
      || die "CMake failed configure step"

make -j `nproc` || die "Build failed"

if ${DO_STRIP} ; then
    echo "Stripping binary"
    strip bin/*
#    strip /usr/local/lib/libboost*
fi

cp -v bin/* /artifacts/bin
#cp -rv /usr/local/lib/libboost* /artifacts/lib

echo "Building deb package..."
if [ "${BUILD_DEB}" = true ] ; then
    cpack || die "Failed to build Debian (.deb) package"
    cp -rv *.deb /artifacts/deb
fi
