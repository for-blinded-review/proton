add_executable(wandbox
    wandbox-origin.cpp
    g.cpp
)

target_link_libraries(wandbox pthread fibsched c++abi)

add_executable(wandbox-bcontext
    wandbox-bcontext.cpp
)

target_link_libraries(wandbox-bcontext proton_co pthread fibsched)


add_executable(wandbox-proton
    wanbox.cpp
    generator.cpp
    g.cpp
)

target_link_libraries(wandbox-proton proton_co pthread fibsched)
