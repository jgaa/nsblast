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

set_property(TARGET dns_messages_tests PROPERTY CXX_STANDARD 20)

add_dependencies(dns_messages_tests
    nsblastlib
    ${DEPENDS_GTEST}
    )

target_include_directories(dns_messages_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(dns_messages_tests
    ${GTEST_LIBRARIES}
    yahat
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


####### db_tests

add_executable(db_tests
    db_tests.cpp
    TmpDb.h
    test_res.cpp test_res.h
    )

set_property(TARGET db_tests PROPERTY CXX_STANDARD 20)

add_dependencies(db_tests
    nsblastlib
     ${DEPENDS_GTEST}
    )

target_include_directories(db_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(db_tests
    ${GTEST_LIBRARIES}
    yahat
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


####### api_tests

add_executable(api_tests
    api_tests.cpp
    TmpDb.h
    test_res.cpp test_res.h
    )

set_property(TARGET api_tests PROPERTY CXX_STANDARD 20)

add_dependencies(api_tests
    nsblastlib
    externalYahat
     ${DEPENDS_GTEST}
    )

target_include_directories(api_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(api_tests
    ${GTEST_LIBRARIES}
    nsblastlib
    yahat
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


####### dns_engine_tests

add_executable(dns_engine_tests
    dns_engine_tests.cpp
    TmpDb.h
    test_res.cpp test_res.h
    )

set_property(TARGET dns_engine_tests PROPERTY CXX_STANDARD 20)

add_dependencies(dns_engine_tests
    nsblastlib
    externalYahat
     ${DEPENDS_GTEST}
    )

target_include_directories(dns_engine_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(dns_engine_tests
    ${GTEST_LIBRARIES}
    nsblastlib
    yahat
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

add_test(NAME dns_engine_tests COMMAND dns_engine_tests)



####### auth_tests

add_executable(auth_tests
    auth_tests.cpp
    TmpDb.h
    test_res.cpp test_res.h
    )

set_property(TARGET auth_tests PROPERTY CXX_STANDARD 20)

add_dependencies(auth_tests
    nsblastlib
     ${DEPENDS_GTEST}
    )

target_include_directories(auth_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(auth_tests
    ${GTEST_LIBRARIES}
    nsblastlib
    yahat
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

####### replication_tests

add_executable(replication_tests
    replication_tests.cpp
    TmpDb.h
    test_res.cpp test_res.h
    )

set_property(TARGET replication_tests PROPERTY CXX_STANDARD 20)

add_dependencies(replication_tests
    nsblastlib
     ${DEPENDS_GTEST}
    )

target_include_directories(replication_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(replication_tests
    ${GTEST_LIBRARIES}
    nsblastlib
    yahat
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

add_test(NAME replication_tests COMMAND replication_tests)

########## Run the tests

if (NSBLAST_RUN_TESTS)

add_custom_target(run_unit_test ALL
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS dns_messages_tests db_tests api_tests dns_engine_tests auth_tests)

endif()
