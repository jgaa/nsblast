cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED NSBLAST_VERSION)
    set(NSBLAST_VERSION 0.5.1)
endif()
add_definitions(-DNSBLAST_VERSION=\"${NSBLAST_VERSION}\")

project(nsblast-all
    DESCRIPTION "Massively scalable authoritative DNS server"
    HOMEPAGE_URL https://github.com/jgaa/nsblast
    VERSION ${NSBLAST_VERSION}
    LANGUAGES CXX)

set(NSBLAST_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NSBLAST_WITH_TESTS "Enable Tests" ON)
option(NSBLAST_WITH_DOCS "Generate documentation" OFF)
option(NSBLAST_USE_VALGRIND "Enable Valgrind" OFF)
option(NSBLAST_RUN_TESTS "Run unit-tests as part of the build" ON)
option(NSBLAST_CLUSTER "Build nsblast for seimi-automatic Cluster Mode" ON)
option(NSBLAST_WITH_SWAGGER "Enable Swagger" ON)
option(NSBLAST_WITH_UI "Enable UI" ON)
option(NSBLAST_UI_ALLOW_HTTP_LOGIN "Allow HTTP login checks in UI build (local testing only)" OFF)
option(NSBLAST_BOOST_USE_STATIC_LIBS "Link statically with boost libraries" ON)
option(NSBLAST_FETCHCONTENT_UPDATES_DISCONNECTED "Disable FetchContent update checks after first download" OFF)

if (NSBLAST_CLUSTER)
    add_definitions(-DNSBLAST_CLUSTER=1)
endif()

if (NSBLAST_WITH_SWAGGER)
    add_definitions(-DNSBLAST_WITH_SWAGGER=1)
endif()

if (NSBLAST_WITH_UI)
    find_program(NPM_EXECUTABLE NAMES npm HINTS /usr)
    if (NOT NPM_EXECUTABLE)
        message(SEND_ERROR "Failed to find npm executable")
    endif()
    add_definitions(-DNSBLAST_WITH_UI=1)
endif()

if (NSBLAST_USE_VALGRIND)
   set (VALDGRIND_ARGS -DBOOST_USE_VALGRIND=1)
endif()

if (NSBLAST_BOOST_USE_STATIC_LIBS)
    message("Using static boost libraries")
    set(Boost_USE_STATIC_LIBS ON CACHE INTERNAL "")
else()
    set(Boost_USE_STATIC_LIBS OFF CACHE INTERNAL "")
endif()


option(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES "Allows us to avoid warnings (mostly) because asio is not updated between boost versions" ON)
if(NSBLAST_SILENCE_BOOST_DEPRICATED_MESSAGES)
    add_definitions(-DBOOST_ALLOW_DEPRECATED_HEADERS=1 -DBoost_NO_WARN_NEW_VERSIONS=1)
endif()

include(GNUInstallDirs)
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ${NSBLAST_FETCHCONTENT_UPDATES_DISCONNECTED})

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(BZip2 REQUIRED)
find_package(Doxygen)

# Find LZ4 manually
find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATH_SUFFIXES lz4)
find_library(LZ4_LIBRARY NAMES lz4)

if(NOT LZ4_INCLUDE_DIR OR NOT LZ4_LIBRARY)
    message(FATAL_ERROR "LZ4 library not found. Please install liblz4-dev.")
endif()

find_package(Snappy REQUIRED)

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

# Get the current Git branch name
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Git branch: ${GIT_BRANCH}")
add_compile_definitions(GIT_BRANCH="${GIT_BRANCH}")

find_package(RocksDB REQUIRED)
message("RocksDB lib: ${ROCKSDB_LIBRARIES}")

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.85)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED NO_MODULE COMPONENTS
    program_options
    date_time
    coroutine
    context
    chrono
    regex
    json
    url
    )
set(NSBLAST_BOOST_LIBS
    Boost::program_options
    Boost::date_time
    Boost::coroutine
    Boost::context
    Boost::chrono
    Boost::regex
    Boost::json
    Boost::url
)

# add_library(boost INTERFACE IMPORTED)
# set_property(TARGET boost PROPERTY
#     INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

set(GLAD_WITH_TESTS OFF)
set(GLAD_WITH_EXAMPLES OFF)
set(YAHAT_WITH_EXAMPLES OFF)
set(YAHAT_WITH_METRICS ON)
set(USE_LOGFAULT=ON)

FetchContent_Declare(
    logfault_dep
    GIT_REPOSITORY https://github.com/jgaa/logfault.git
    GIT_TAG origin/HEAD
)
FetchContent_Declare(
    mkres_dep
    GIT_REPOSITORY https://github.com/jgaa/mkres.git
    GIT_TAG origin/HEAD
)
FetchContent_Declare(
    glad_dep
    GIT_REPOSITORY https://github.com/jgaa/glad.git
    GIT_TAG origin/HEAD
)
FetchContent_Declare(
    yahat_dep
    GIT_REPOSITORY https://github.com/jgaa/yahat-cpp.git
    GIT_TAG origin/HEAD
)
FetchContent_MakeAvailable(logfault_dep mkres_dep glad_dep yahat_dep)

include_directories(${logfault_dep_SOURCE_DIR}/include)
include_directories(${glad_dep_SOURCE_DIR}/include)

if (TARGET yahat)
    target_link_libraries(yahat PUBLIC
        Boost::program_options
        Boost::coroutine
        Boost::context
        Boost::chrono
        Boost::json
        Boost::url
    )
endif()

add_subdirectory(src/lib)
add_subdirectory(src/nsblast)
#set(MKRES "${EXTERNAL_PROJECTS_PREFIX}/installed/bin/mkres")

if (NSBLAST_WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()


message(STATUS "Components to pack: ${CPACK_COMPONENTS_ALL}")
