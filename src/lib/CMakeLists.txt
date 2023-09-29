project(nsblastlib VERSION ${NSBLAST_VERSION} LANGUAGES CXX)

message(STATUS "Configuring ${PROJECT_NAME} ${NSBLAST_VERSION}")

if (NSBLAST_WITH_SWAGGER)
# See https://lastviking.eu/code_generator.html
add_custom_command(
    COMMAND mkres nsblast::lib::embedded swagger_files_ swagger_res.cpp swagger_res.h ${NSBLAST_ROOT}/swagger/*
    DEPENDS ${NSBLAST_ROOT}/swagger/index.html ${NSBLAST_ROOT}/swagger/swagger.yaml mkres
    OUTPUT swagger_res.cpp swagger_res.h
    COMMENT "Embedding swagger..."
    )
    set(LIB_SWAGGER_FILES swagger_res.cpp swagger_res.h)
endif()

ADD_SUBDIRECTORY(proto)

if (NSBLAST_CLUSTER)
    set(LIB_CLUSTER_FILES GrpcPrimary.cpp GrpcPrimary.h GrpcFollow.cpp GrpcFollow.h)
endif()

add_library(${PROJECT_NAME}
    ${LIB_CLUSTER_FILES}
    ${LIB_SWAGGER_FILES}
    ${NSBLAST_ROOT}/include/nsblast/DnsEngine.h
    ${NSBLAST_ROOT}/include/nsblast/DnsMessages.h
    ${NSBLAST_ROOT}/include/nsblast/ResourceIf.h
    ${NSBLAST_ROOT}/include/nsblast/Server.h
    ${NSBLAST_ROOT}/include/nsblast/detail/write_labels.hpp
    ${NSBLAST_ROOT}/include/nsblast/errors.h
    ${NSBLAST_ROOT}/include/nsblast/logging.h
    ${NSBLAST_ROOT}/include/nsblast/nsblast.h
    ${NSBLAST_ROOT}/include/nsblast/util.h
    AuthMgr.cpp
    AuthMgr.h
    BackupMgr.cpp
    BackupMgr.h
    DnsEngine.cpp
    DnsMessages.cpp
    FollowerReplication.cpp
    FollowerReplication.h
    Notifications.cpp
    Notifications.h
    PrimaryReplication.cpp
    PrimaryReplication.h
    ResourceIf.cpp
    RestApi.cpp
    RestApi.h
    RocksDbResource.cpp
    RocksDbResource.h
    Server.cpp
    Slave.cpp
    Slave.h
    SlaveMgr.cpp
    SlaveMgr.h
    proto_util.h

    util.cpp
    )

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${PROTOBUF_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE $Boost_INCLUDE_DIR
    PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE ${VALDGRIND_ARGS})

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
add_dependencies(${PROJECT_NAME} rocksdb logfault mkres externalYahat glad ${GRPC_LIBS})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        $<BUILD_INTERFACE:${Protobuf_LIBRARIES}>
        $<BUILD_INTERFACE:proto>
    )

#install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config
#    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})  # This is for Windows
#install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

#install(EXPORT ${PROJECT_NAME}Config DESTINATION share/${PROJECT_NAME}/cmake)

message(STATUS "Components to pack (nsblast): ${CPACK_COMPONENTS_ALL}")

