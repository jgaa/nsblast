project(nsblastlib VERSION ${NSBLAST_VERSION} LANGUAGES CXX)

message(STATUS "Configuring ${PROJECT_NAME} ${NSBLAST_VERSION}")

#protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS data.proto)

add_custom_command(
    COMMAND ${PROTOC} --cpp_out ${CMAKE_CURRENT_BINARY_DIR} --proto_path ${CMAKE_CURRENT_SOURCE_DIR} data.proto
    DEPENDS protobuf-external data.proto
    OUTPUT data.pb.cc data.pb.h
    COMMENT "Compiling protobuf files"
    )

# See https://lastviking.eu/code_generator.html
add_custom_command(
    COMMAND mkres nsblast::lib::embedded swagger_files_ swagger_res.cpp swagger_res.h ${NSBLAST_ROOT}/swagger/*
    DEPENDS ${NSBLAST_ROOT}/swagger/index.html ${NSBLAST_ROOT}/swagger/swagger.yaml mkres
    OUTPUT swagger_res.cpp swagger_res.h
    COMMENT "Embedding swagger..."
    )

add_library(${PROJECT_NAME}
    data.pb.cc data.pb.h
    data.proto
    ${NSBLAST_ROOT}/include/nsblast/nsblast.h
    ${NSBLAST_ROOT}/include/nsblast/logging.h
    ${NSBLAST_ROOT}/include/nsblast/DnsEngine.h
    ${NSBLAST_ROOT}/include/nsblast/Admin.h
    Db.h
    Db.cpp
    DnsEngine.cpp
    HttpServer.h
    HttpServer.cpp
    RestApi.h
    RestApi.cpp
    swagger_res.cpp
    swagger_res.h
    DnsServer.cpp
    DnsServer.h
    )

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${Protobuf_INCLUDE_DIRS}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    $<INSTALL_INTERFACE:include>
    PRIVATE $Boost_INCLUDE_DIR
    PRIVATE src)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
add_dependencies(${PROJECT_NAME} rocksdb logfault mkres protobuf-external)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})  # This is for Windows
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ${PROJECT_NAME}Config DESTINATION share/${PROJECT_NAME}/cmake)
