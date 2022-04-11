list(APPEND IRC_SOURCES
    irc.cpp
)

qt5_wrap_cpp(IRC_SOURCES
    common.hpp
    irc.hpp
)

add_library(irc STATIC)
target_compile_features(irc PUBLIC cxx_std_17)
set_target_properties(irc PROPERTIES CXX_EXTENSIONS 0)
target_sources(irc PRIVATE ${IRC_SOURCES})
target_include_directories(irc PUBLIC .)
target_link_libraries(irc PUBLIC
    Qt5::Core
    Qt5::Network
    fmt::fmt
)
