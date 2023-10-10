cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED NSBLAST_VERSION)
    set(NSBLAST_VERSION 0.0.1)
endif()
add_definitions(-DNSBLAST_VERSION=\"${NSBLAST_VERSION}\")

project(nsblast-all
    DESCRIPTION "Massively scalable authoritative DNS server"
    HOMEPAGE_URL https://github.com/jgaa/nsblast
    VERSION ${NSBLAST_VERSION}
    LANGUAGES CXX)

set(NSBLAST_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(NSBLAST_WITH_TESTS "Enable Tests" ON)
option(NSBLAST_WITH_DOCS "Generate documentation" ON)
option(NSBLAST_USE_VALGRIND "Enable Valgrind" OFF)
option(NSBLAST_RUN_TESTS "Run unit-tests as part of the build" OFF)
option(NSBLAST_CLUSTER "Build nsblast for seimi-automatic Cluster Mode" ON)
option(NSBLAST_WITH_SWAGGER "Enable Swagger" ON)

if (NSBLAST_CLUSTER)
    add_definitions(-DNSBLAST_CLUSTER=1)
endif()

if (NSBLAST_WITH_SWAGGER)
    add_definitions(-DNSBLAST_WITH_SWAGGER=1)
endif()

if (NSBLAST_USE_VALGRIND)
   set (VALDGRIND_ARGS -DBOOST_USE_VALGRIND=1)
endif()

option(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES "Allows us to avoid warnings (mostly) because asio is not updated between boost versions" ON)
if(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES)
    add_definitions(-DBOOST_ALLOW_DEPRECATED_HEADERS=1 -DBoost_NO_WARN_NEW_VERSIONS=1)
endif()

include(GNUInstallDirs)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(BZip2 REQUIRED)
find_package(Doxygen)

if (NOT  DOXYGEN_FOUND)
    message("Doxygen not found. Disabling document generation")
    set(NSBLAST_WITH_DOCS OFF)
endif()

if (NSBLAST_WITH_TESTS)
    find_package(GTest REQUIRED)
endif()

if (NSBLAST_WITH_DOCS)
    find_package(Doxygen REQUIRED)

    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/doc/nsblast.doxygen)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    add_custom_target(doc_doxygen ALL
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM )
endif() # docs

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.82)
endif()

option(Boost_USE_STATIC_LIBS "Link statically with boost libraries" ON)
find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    locale
    program_options
    date_time
    coroutine
    context
    chrono
    regex
    json
    url
    )

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

include(cmake/3rdparty.cmake)

add_subdirectory(src/lib)
add_subdirectory(src/mkres)
add_subdirectory(src/nsblast)

if (NSBLAST_WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()


message(STATUS "Components to pack: ${CPACK_COMPONENTS_ALL}")
