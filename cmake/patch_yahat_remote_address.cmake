set(header "${YAHAT_SOURCE_DIR}/include/yahat/HttpServer.h")
set(source "${YAHAT_SOURCE_DIR}/src/HttpServer.cpp")

file(READ "${header}" header_text)
if (NOT header_text MATCHES "std::string remote_address;")
    string(REPLACE
        "    std::vector<std::pair<std::string_view, std::string_view>> cookies;\n    bool is_https{false};\n"
        "    std::vector<std::pair<std::string_view, std::string_view>> cookies;\n    bool is_https{false};\n    std::string remote_address;\n"
        header_text
        "${header_text}")
    file(WRITE "${header}" "${header_text}")
endif()

file(READ "${source}" source_text)
if (NOT source_text MATCHES "request\\.remote_address = lr\\.remote\\.address\\(\\)\\.to_string\\(\\);")
    string(REPLACE
        "        lr.remote =  beast::get_lowest_layer(stream).socket().remote_endpoint();\n        lr.local = beast::get_lowest_layer(stream).socket().local_endpoint();\n        lr.location = req.base().target();\n"
        "        lr.remote =  beast::get_lowest_layer(stream).socket().remote_endpoint();\n        lr.local = beast::get_lowest_layer(stream).socket().local_endpoint();\n        lr.location = req.base().target();\n        request.remote_address = lr.remote.address().to_string();\n"
        source_text
        "${source_text}")
    file(WRITE "${source}" "${source_text}")
endif()
