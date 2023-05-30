INCLUDE(FindProtobuf)
FIND_PACKAGE(Protobuf REQUIRED)
if (NSBLAST_CLUSTER)
    find_package(gRPC CONFIG REQUIRED)
    set(GRPC_LIB gRPC::grpc gRPC::grpc++)
    set(GRPC_SRC_FILES nsblast-grpc.proto)
endif()

set(PROTO_FILES
    nsblast.proto ${GRPC_SRC_FILES}
)

add_library(proto ${PROTO_FILES})
target_link_libraries(proto
    PUBLIC
        protobuf::libprotobuf
        ${GRPC_LIB}
)

target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

#
# Compile protobuf and grpc files in proto target to cpp
#
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET proto LANGUAGE cpp)

if (NSBLAST_CLUSTER)
    protobuf_generate(TARGET proto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")
endif()

#INCLUDE_DIRECTORIES(${PROTOBUF_INCLUDE_DIR})
#PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HEADER nsblast.proto)
#ADD_LIBRARY(proto ${PROTO_HEADER} ${PROTO_SRC})
