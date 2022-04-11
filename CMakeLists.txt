cmake_minimum_required(VERSION 3.18)

project(ircfs
    VERSION 0.1
    LANGUAGES C CXX
)

find_package(fuse REQUIRED)
find_package(fmt REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(Qt5 COMPONENTS Network REQUIRED)

set(KQ_DEFINES "FMT_HEADER_ONLY" "FUSE_USE_VERSION=30")

add_subdirectory(irc)
add_subdirectory(src)
