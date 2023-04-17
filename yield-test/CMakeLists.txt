add_executable(
    yield-test
    yield-test.cpp
)
target_link_libraries(yield-test PUBLIC proton_co pthread c++abi)
target_include_directories(yield-test PUBLIC ../)