project(mkres VERSION 1.0 LANGUAGES CXX)

add_executable(mkres main.cpp)
target_link_libraries(mkres stdc++fs)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
