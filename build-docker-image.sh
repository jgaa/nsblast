#!/bin/bash

## General build script for a CMake project to
## build a container-image from the deliverables

project=nsblast
image_repro=lastviking

docker_run_args=""
tag=latest
strip=false
debug=false
push=false
cmake_build_type=Release
image_tag=$project
build_image="${project}bld:latest"
scriptname=`basename "$0"`

if [ -z ${BUILD_DIR+x} ]; then
    BUILD_DIR="${HOME}/${project}-build-image"
fi

usage() {
  echo "Usage: ${scriptname} [options]"
  echo "Builds ${project} to a container image"
  echo "Options:"
  echo "  --debug       Compile with debugging enabled"
  echo "  --strip       Strip the binary (makes backtraces less useful)"
  echo "  --push        Push the image to a docker registry"
  echo "  --tag tagname Tag to '--push' to. Defaults to 'latest'"
  echo "  --scripted    Assume that the command is run from a script"
  echo "  --help        Show help and exit."
  echo
  echo "Environment variables"
  echo "  BUILD_DIR     Directory to build with CMake. Default: ${BUILD_DIR}"
  echo "  TARGET        Target image. Defaults to ${project}:<tagname>"
  echo "  REGISTRY      Registry to '--push' to. Defaults to ${image_repro}"
  echo "  SOURCE_DIR    Directory to the source code. Defaults to the current dir"
  echo
}

# Make a build-container that contains the compiler and dev-libraries
# for the build. We don't want all that in the destination container.
build_bldimage() {
    pushd docker
    echo Buiding build-image
    docker build -f Dockerfile.build -t ${build_image} . || die
    popd
}

die() {
    echo "$*" 1>&2
    exit 1;
}

while [ $# -gt 0 ];  do
    case "$1" in
        --debug)
            shift
            cmake_build_type=Debug
            ;;

        --strip)
            shift
            strip=true
            ;;

        --push)
            shift
            push=true
            ;;

        --tag)
            shift
            tag=$1
            shift
            ;;

        --scripted)
            shift
            docker_run_args="-t"
            ;;

        --help)
            usage
            exit 0
            ;;

        -h)
            usage
            exit 0
            ;;

        *)
            echo "ERROR: Unknown parameter: $1"
            echo
            usage
            exit 1
            ;;
    esac
done


if [ -z ${TARGET+x} ]; then
    TARGET=${project}:${tag}
fi

if [ -z ${REGISTRY+x} ]; then
    REGISTRY=${image_repro}
fi

if [ -z ${SOURCE_DIR+x} ]; then
    SOURCE_DIR=`pwd`
fi

echo "Starting the build process in dir: ${BUILD_DIR}"

build_bldimage

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p ${BUILD_DIR}
fi

pushd ${BUILD_DIR}

artifacts_dir="${BUILD_DIR}/artifacts"


echo rm -rf $artifacts_dir
mkdir -p ${artifacts_dir}/lib
mkdir -p ${artifacts_dir}/bin

if [ ! -d "build" ]; then
    mkdir build
fi

echo "==================================================="
echo "Building ${project} libraries and binaries"
echo "Artifacts to: ${artifacts_dir}"
echo "==================================================="

docker run                                                      \
    --rm ${docker_run_args}                                          \
    -u $UID                                                          \
    --name "${project}-build"                                        \
    -e DO_STRIP=${strip}                                             \
    -e BUILD_DIR=/build                                              \
    -e BUILD_TYPE="${cmake_build_type}"                              \
    -v ${SOURCE_DIR}:/src                                            \
    -v ${BUILD_DIR}/build:/build                                     \
    -v ${artifacts_dir}:/artifacts                                   \
    ${build_image}                                                   \
    /src/docker/build-${project}.sh                                  \
    || die


target_image="${REGISTRY}/${TARGET}"
echo "==================================================="
echo "Making target: ${target_image}"
echo "==================================================="
cp -v "${SOURCE_DIR}/docker/Dockerfile.${project}" ${artifacts_dir}/Dockerfile
pushd ${artifacts_dir}

docker build -t ${target_image} . || die "Failed to make target: ${target_image}"

popd # ${artifacts_dir}
popd # ${BUILD_DIR}
