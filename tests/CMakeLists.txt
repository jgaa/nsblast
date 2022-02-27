project(unittests LANGUAGES CXX)

add_custom_command(
    COMMAND mkres nsblast::test::embedded test_files_ test_res.cpp test_res.h ${NSBLAST_ROOT}/tests/embedded/*
    DEPENDS ${NSBLAST_ROOT}/tests/embedded/test.txt mkres
    OUTPUT test_res.cpp test_res.h
    COMMENT "Embedding test files..."
    )

add_executable(http_tests
    http_tests.cpp
    test_res.cpp test_res.h
    )

set_property(TARGET http_tests PROPERTY CXX_STANDARD 17)

add_dependencies(http_tests
    nsblastlib
    googletest
    )

target_include_directories(http_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
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


####### api_tests

add_executable(api_tests
    api_tests.cpp
    test_res.cpp test_res.h
    )

set_property(TARGET api_tests PROPERTY CXX_STANDARD 17)

add_dependencies(api_tests
    nsblastlib
    googletest
    )

target_include_directories(api_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(api_tests
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

add_test(NAME api_tests COMMAND api_tests)

####### db_tests

add_executable(db_tests
    db_tests.cpp
    test_res.cpp test_res.h
    )

set_property(TARGET db_tests PROPERTY CXX_STANDARD 17)

add_dependencies(db_tests
    nsblastlib
    googletest
    )

target_include_directories(db_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(db_tests
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

add_test(NAME db_tests COMMAND db_tests)
