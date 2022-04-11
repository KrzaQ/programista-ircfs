list(APPEND IRCFS_SOURCES
    main.cpp
)

add_executable(ircfs ${IRCFS_SOURCES})
target_compile_features(ircfs PUBLIC cxx_std_17)
set_target_properties(ircfs PROPERTIES CXX_EXTENSIONS 0)
target_link_libraries(ircfs PUBLIC
    ${FUSE_LIBRARIES}
    fmt::fmt
    irc
)
target_compile_definitions(ircfs PRIVATE
    ${FUSE_DEFINITIONS}
    ${KQ_DEFINES}
)
