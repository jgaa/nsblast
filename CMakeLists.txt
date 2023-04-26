cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED NSBLAST_VERSION)
    set(NSBLAST_VERSION 0.0.1)
endif()
add_definitions(-DNSBLAST_VERSION=\"${NSBLAST_VERSION}\")

project(nsblast-all VERSION ${NSBLAST_VERSION} LANGUAGES CXX)
set(NSBLAST_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(NSBLAST_WITH_TESTS "Enable Tests" ON)
option(NSBLAST_WITH_DOCS "Generate documentation" ON)
option(NSBLAST_USE_VALGRIND "Enable Valgrind" OFF)

if (NSBLAST_USE_VALGRIND)
   set (VALDGRIND_ARGS -DBOOST_USE_VALGRIND=1)
endif()

option(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES "Allows us to avoid warnings (mostly) because asio is not updated between boost versions" ON)
if(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES)
    add_definitions(-DBOOST_ALLOW_DEPRECATED_HEADERS=1)
endif()

include(GNUInstallDirs)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads)
find_package(BZip2 REQUIRED)
find_package(Protobuf REQUIRED)

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
    set(USE_BOOST_VERSION 1.81)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    program_options
    date_time
    coroutine
    context
    chrono
    regex
    json
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
