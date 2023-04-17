add_library(
    socket-test STATIC
    socket-test.cpp
)
target_link_libraries(socket-test PUBLIC pthread c++abi)
target_include_directories(socket-test PUBLIC ../)