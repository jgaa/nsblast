project(nsblastlib VERSION ${NSBLAST_VERSION} LANGUAGES CXX)

message(STATUS "Configuring ${PROJECT_NAME} ${NSBLAST_VERSION}")

# See https://lastviking.eu/code_generator.html
#add_custom_command(
#    COMMAND mkres nsblast::lib::embedded swagger_files_ swagger_res.cpp swagger_res.h ${NSBLAST_ROOT}/swagger/*
#    DEPENDS ${NSBLAST_ROOT}/swagger/index.html ${NSBLAST_ROOT}/swagger/swagger.yaml mkres
#    OUTPUT swagger_res.cpp swagger_res.h
#    COMMENT "Embedding swagger..."
#    )

add_library(${PROJECT_NAME}
    ${NSBLAST_ROOT}/include/nsblast/nsblast.h
    ${NSBLAST_ROOT}/include/nsblast/logging.h
    ${NSBLAST_ROOT}/include/nsblast/DnsMessages.h
    ${NSBLAST_ROOT}/include/nsblast/ApiEngine.h
    DnsMessages.cpp
    ApiEngine.cpp
    )

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE $Boost_INCLUDE_DIR
    PRIVATE src)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
add_dependencies(${PROJECT_NAME} rocksdb logfault mkres yahat)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})  # This is for Windows
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ${PROJECT_NAME}Config DESTINATION share/${PROJECT_NAME}/cmake)
