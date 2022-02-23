project(unittests LANGUAGES CXX)

add_executable(http_tests
    http_tests.cpp
    )

set_property(TARGET http_tests PROPERTY CXX_STANDARD 17)

add_dependencies(http_tests
    nsblastlib
    googletest
    )

target_include_directories(http_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NSBLAST_ROOT}/include
    ${NSBLAST_ROOT}/src/lib
    )

target_link_libraries(http_tests
    gtest
    nsblastlib
    ${Protobuf_LIBRARIES}
    ${SNAPPY_LIBRARIES} # Not working
    ${ROCKSDB_LIBRARIES}
    ${Boost_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    stdc++fs
    lz4
    snappy
    ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME http_tests COMMAND http_tests)
