project(nsblastlib VERSION ${NSBLAST_VERSION} LANGUAGES CXX)

message(STATUS "Configuring ${PROJECT_NAME} ${NSBLAST_VERSION}")

if (NSBLAST_WITH_SWAGGER)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/swagger_res.cpp ${CMAKE_CURRENT_BINARY_DIR}/swagger_res.h
        COMMAND mkres --verbose --compression gzip --namespace nsblast::lib::embedded --name Swagger --destination swagger_res --exclude '.*\\.map' ${NSBLAST_ROOT}/swagger/*
        DEPENDS ${NSBLAST_ROOT}/swagger/index.html ${NSBLAST_ROOT}/swagger/swagger.yaml mkres
        COMMENT "Embedding swagger... Dst: ${CMAKE_CURRENT_BINARY_DIR}"
    )

    set(LIB_SWAGGER_FILES ${CMAKE_CURRENT_BINARY_DIR}/swagger_res.cpp ${CMAKE_CURRENT_BINARY_DIR}/swagger_res.h)
endif()

if (NSBLAST_WITH_UI)
    set(UI_DEST ${CMAKE_CURRENT_BINARY_DIR}/ui)
    add_custom_command(
        COMMAND BUILD_PATH=${UI_DEST} ${NPM_EXECUTABLE} --prefix ${NSBLAST_ROOT}/ui/nsblast-ui run build && touch ui_files.stamp
        DEPENDS ${NSBLAST_ROOT}/ui/nsblast-ui/package.json
        OUTPUT ${UI_DEST}asset-manifest.json ui_files.stamp
        COMMENT "Building react based UI..."
        )

        add_custom_target(ui_js ALL DEPENDS ui_files.stamp)

    add_custom_command(
        COMMAND mkres --verbose --recurse --compression gzip --namespace nsblast::lib::embedded --name Ui --destination ui_res --exclude '.*\\.map' ${UI_DEST}/*
        DEPENDS ui_files.stamp mkres
        OUTPUT ui_res.cpp ui_res.h
        COMMENT "Embedding UI ..."
        )

        set(LIB_UI_FILES ui_res.cpp ui_res.h)
endif()

ADD_SUBDIRECTORY(proto)

if (NSBLAST_CLUSTER)
    set(LIB_CLUSTER_FILES GrpcPrimary.cpp GrpcPrimary.h GrpcFollow.cpp GrpcFollow.h)
endif()

add_library(${PROJECT_NAME}
    ${LIB_CLUSTER_FILES}
    ${LIB_SWAGGER_FILES}
    ${LIB_UI_FILES}
    ${NSBLAST_ROOT}/include/nsblast/DnsEngine.h
    ${NSBLAST_ROOT}/include/nsblast/DnsMessages.h
    ${NSBLAST_ROOT}/include/nsblast/ResourceIf.h
    ${NSBLAST_ROOT}/include/nsblast/Server.h
    ${NSBLAST_ROOT}/include/nsblast/certs.h
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
    Metrics.cpp
    Metrics.h
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
    certs.cpp
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

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Using g++, adding -Winterference-size flag")
    target_compile_options(${PROJECT_NAME} PUBLIC -Wno-interference-size)
endif()

# g++ don't like long constexpr loops
# We need it for the embedded resources
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -fconstexpr-loop-limit=1000000)
endif()

add_dependencies(${PROJECT_NAME} mkres logfault yahat glad ${GRPC_LIBS})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        $<BUILD_INTERFACE:${Protobuf_LIBRARIES}>
        $<BUILD_INTERFACE:${ROCKSDB_LIBRARIES}>
        $<BUILD_INTERFACE:proto>
        $<BUILD_INTERFACE:yahat>
    )

message(STATUS "Components to pack (nsblast): ${CPACK_COMPONENTS_ALL}")

