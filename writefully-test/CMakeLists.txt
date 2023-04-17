add_executable(
    socket-test
    socket-test.cpp
)
target_link_libraries(socket-test PUBLIC proton_co fibsched pthread c++abi)
target_include_directories(socket-test PUBLIC ../)