project(unittests LANGUAGES CXX)

add_custom_command(
    COMMAND mkres nsblast::test::embedded test_files_ test_res.cpp test_res.h ${NSBLAST_ROOT}/tests/embedded/*
    DEPENDS ${NSBLAST_ROOT}/tests/embedded/test.txt mkres
    OUTPUT test_res.cpp test_res.h
    COMMENT "Embedding test files..."
    )



####### dns_messages_tests

add_executable(dns_messages_tests
    dns_messages_tests.cpp
    )

set_property(TARGET dns_messages_tests PROPERTY CXX_STANDARD 17)

add_dependencies(dns_messages_tests
    nsblastlib
    googletest
    )

target_include_directories(dns_messages_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(dns_messages_tests
    gtest
    nsblastlib
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

add_test(NAME dns_messages_tests COMMAND dns_messages_tests)

