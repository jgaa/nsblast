#!/bin/bash

## General build script for a CMake project to
## build a container-image from the deliverables

project=nsblast
image_repro=jgaafromnorth

docker_run_args=""
tag=latest
strip=false
debug=false
push=false
clean=false
build_deb=false
run_tests=ON
swagger=true
ui=OFF
cmake_build_type=RelWithDebInfo
image_tag=$project
scriptname=`basename "$0"`
version=v`grep " set(NSBLAST_VERSION" CMakeLists.txt | xargs | cut -f 2 -d ' ' | cut -f1 -d')'`

if [ -z ${BUILD_DIR+x} ]; then
    BUILD_DIR="${HOME}/${project}-build-image"
else
    BUILD_DIR="${BUILD_DIR}/${project}-docker-build"
fi

usage() {
  echo "Usage: ${scriptname} [options]"
  echo "Builds ${project} to a container image"
  echo "Options:"
  echo "  --debug       Compile with debugging enabled"
  echo "  --strip       Strip the binary (makes backtraces less useful)"
  echo "  --clean       Perform a full, new build."
  echo "  --ui          Build with embedded UI (requires npm in build stage)"
  echo "  --no-swagger  Build without swagger (smaller binary)"
  echo "  --deb         Build a deb package for Debian as well"
  echo "  --push        Push the image to a docker registry"
  echo "  --tag tagname Tag to '--push' to. Defaults to 'latest'"
  echo "  --version ver Version to tag. Defaults to '${version}'"
  echo "  --scripted    Assume that the command is run from a script"
  echo "  --help        Show help and exit."
  echo "  --skip-tests  Skip running the unit-tests as part of the build"
  echo
  echo "Environment variables"
  echo "  BUILD_DIR     Directory to build with CMake. Default: '${BUILD_DIR}'"
  echo "  TARGET        Target image. Defaults to ${project}:<tagname>"
  echo "  REGISTRY      Registry to '--push' to. Defaults to '${image_repro}'"
  echo "  SOURCE_DIR    Directory to the source code. Defaults to the current dir"
  echo
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

         --no-swagger)
            shift
            swagger=false
            ;;

        --deb)
            shift
            build_deb=true
            ;;

        --skip-tests)
            shift
            run_tests=OFF
            ;;
            
        --clean)
            shift
            clean=true
            ;;

        --ui)
            shift
            ui=ON
            ;;

        --tag)
            shift
            tag=$1
            shift
            ;;

        --version)
            shift
            version=$1
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

if [ "$clean" = true ] ; then
  if [ -d "${BUILD_DIR}" ]; then
    echo "Cleaning the build-dir: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
  fi
fi

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p ${BUILD_DIR}
fi

if [ "$build_deb" = true ] ; then
    echo "WARNING: --deb is ignored in the multi-stage container build."
fi

if [ -n "${docker_run_args}" ] ; then
    echo "WARNING: --scripted is ignored in the multi-stage container build."
fi

echo "==================================================="
echo "Building ${project} multi-stage container image"
echo "==================================================="

target_image="${REGISTRY}/${TARGET}"
echo "==================================================="
echo "Making target: ${target_image}"
echo "==================================================="

docker build \
    -f "${SOURCE_DIR}/docker/Dockerfile.${project}" \
    --build-arg BUILD_TYPE="${cmake_build_type}" \
    --build-arg DO_STRIP="${strip}" \
    --build-arg NSBLAST_RUN_TESTS="${run_tests}" \
    --build-arg NSBLAST_WITH_SWAGGER="$( [ "$swagger" = true ] && echo ON || echo OFF )" \
    --build-arg NSBLAST_WITH_UI="${ui}" \
    -t ${target_image} \
    "${SOURCE_DIR}" \
    || die "Failed to make target: ${target_image}"

if [ "$push" = true ] ; then
    docker push ${target_image}

    if [[ -n "${version// /}" ]]; then
        vtag=${REGISTRY}/${project}:${version}
        echo "Tagging and pushing: ${vtag}"
        docker tag ${target_image} ${vtag}
        docker push ${vtag}
    fi
fi
