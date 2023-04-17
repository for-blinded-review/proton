add_executable(hanoi-test
    hanoi.cpp
)

target_link_libraries(hanoi-test pthread proton_co fibsched)
