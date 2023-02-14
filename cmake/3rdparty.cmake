set(EXTERNAL_PROJECTS_PREFIX ${CMAKE_BINARY_DIR}/external-projects)
set(EXTERNAL_PROJECTS_INSTALL_PREFIX ${EXTERNAL_PROJECTS_PREFIX}/installed)

include(GNUInstallDirs)
include(ExternalProject)

# MUST be called before any add_executable() # https://stackoverflow.com/a/40554704/8766845
link_directories(${EXTERNAL_PROJECTS_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
include_directories($<BUILD_INTERFACE:${EXTERNAL_PROJECTS_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

ExternalProject_Add(logfault
    PREFIX "${EXTERNAL_PROJECTS_PREFIX}"
    GIT_REPOSITORY "https://github.com/jgaa/logfault.git"
    GIT_TAG "master"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EXTERNAL_PROJECTS_INSTALL_PREFIX}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_GENERATOR='${CMAKE_GENERATOR}'
        -DCMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'
)

ExternalProject_Add(externalYahat
    PREFIX "${EXTERNAL_PROJECTS_PREFIX}"
    GIT_REPOSITORY "https://github.com/jgaa/yahat-cpp.git"
    GIT_TAG "main"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EXTERNAL_PROJECTS_INSTALL_PREFIX}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_GENERATOR='${CMAKE_GENERATOR}'
        -DCMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'
        -DYAHAT_WITH_EXAMPLES=OFF
        -DUSE_LOGFAULT=ON
        -DBOOST_ROOT=${BOOST_ROOT}
        -DUSE_BOOST_VERSION=${USE_BOOST_VERSION}
        -DLOGFAULT_ROOT='${EXTERNAL_PROJECTS_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}'
)

add_dependencies(externalYahat logfault)


# If we compile the tests; download and install gtest if it's not found on the target
# On ubuntu and debian, you can install `libgtest-dev` to avoid this step.
if (NSBLAST_WITH_TESTS)
    find_package(GTest)
    if (GTest_FOUND)
        message("Using installed googletest")
    else()
        message("Will download and install googletest as a cmake included project")
        set(DEPENDS_GTEST googletest)
        set(GTEST_LIBRARIES gtest)

        if (NOT DEFINED GTEST_TAG)
            set(GTEST_TAG "main")
        endif()

        message("GTEST_TAG: ${GTEST_TAG}")

        ExternalProject_Add(googletest
            GIT_TAG "${GTEST_TAG}"
            PREFIX "${EXTERNAL_PROJECTS_PREFIX}"
            GIT_REPOSITORY https://github.com/google/googletest.git
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX=${EXTERNAL_PROJECTS_INSTALL_PREFIX}
                -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCMAKE_GENERATOR='${CMAKE_GENERATOR}'
                ${GTEST_EXTRA_ARGS}
        )
        set(GTEST_LIB_DIR ${RESTC_EXTERNAL_INSTALLED_LIB_DIR})
    endif()
endif()

ExternalProject_Add(rocksdb
    PREFIX "${EXTERNAL_PROJECTS_PREFIX}"
    GIT_REPOSITORY "https://github.com/facebook/rocksdb.git"
    GIT_TAG "v7.9.2"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EXTERNAL_PROJECTS_INSTALL_PREFIX}
        -DUSE_RTTI=1
        -DPORTABLE=${PORTABLE}
        -DCMAKE_CXX_STANDARD=17
        -DWITH_TESTS=OFF
        -DWITH_TOOLS=OFF
        -DWITH_BENCHMARK_TOOLS=OFF
        -DWITH_CORE_TOOLS=OFF
        -DWITH_SNAPPY=ON
        -DWITH_ZLIB=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=True
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_GENERATOR='${CMAKE_GENERATOR}'
        -DCMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'

     # Required because CMake don't work really well with ninja
     BUILD_BYPRODUCTS external-projects/src/rocksdb-build/librocksdb.a
)

ExternalProject_Get_Property(rocksdb BINARY_DIR)
set(ROCKSDB_LIBRARIES ${BINARY_DIR}/librocksdb.a)
set(ROCKSDB_FOUND TRUE)

