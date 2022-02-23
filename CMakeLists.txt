cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED NSBLAST_VERSION)
    set(NSBLAST_VERSION 0.0.1)
endif()
add_definitions(-DNSBLAST_VERSION=\"${NSBLAST_VERSION}\")

project(nsblast-all VERSION ${NSBLAST_VERSION} LANGUAGES CXX)
set(NSBLAST_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES "Allows us to avoid warnings (mostly) because asio is not updated between boost versions" ON)
if(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES)
    add_definitions(-DBOOST_ALLOW_DEPRECATED_HEADERS=1)
endif()

#option(BOOST_ERROR_CODE_HEADER_ONLY "Work-around for another boost issue" ON)
#if (BOOST_ERROR_CODE_HEADER_ONLY)
    #add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY=1)
#endif()

include(GNUInstallDirs)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads)
find_package(Protobuf REQUIRED)
find_package(BZip2 REQUIRED)
#find_package(LZ4 REQUIRED)

find_package(snappy REQUIRED)

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.74)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    program_options
    context
    coroutine
    regex
    chrono)

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

include(cmake/3rdparty.cmake)

add_subdirectory(src/lib)
add_subdirectory(src/nsblast)
add_subdirectory(tests)
