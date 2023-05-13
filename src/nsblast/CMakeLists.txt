project(nsblast VERSION ${NSBLAST_VERSION} LANGUAGES CXX)

message(STATUS "Configuring ${PROJECT_NAME} ${VUDNS_VERSION}")

add_executable(${PROJECT_NAME}
    main.cpp
    )

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    $<INSTALL_INTERFACE:include>
    PRIVATE src)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
add_dependencies(${PROJECT_NAME} logfault rocksdb nsblastlib externalYahat)

target_link_libraries(${PROJECT_NAME} PRIVATE
    nsblastlib
    yahat
    ${ROCKSDB_LIBRARIES}
    ${Boost_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    stdc++fs
    # TODO: Add macros and check for these libraries
    lz4
    snappy
    ${CMAKE_THREAD_LIBS_INIT}
    )

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}App
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})  # This is for Windows

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    IMPORTED_LOCATION "external-projects/src/rocksdb-build/librocksdb.a"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_definitions(${PROJECT_NAME} PUBLIC -DBOOST_COROUTINES_NO_DEPRECATION_WARNING=1)
target_compile_definitions(${PROJECT_NAME} PUBLIC -DBOOST_COROUTINE_NO_DEPRECATION_WARNING=1)
target_compile_definitions(${PROJECT_NAME} PUBLIC -DAPP_VERSION=\"${VUDNS_VERSION}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE ${VALDGRIND_ARGS})
