cmake_minimum_required(VERSION 3.16.0)
project(fcitx5-ai VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Fcitx5Core REQUIRED)
find_package(CURL REQUIRED)

add_library(fcitx5-ai MODULE
    src/ai.cpp
    src/ThreadPool.cpp
)

target_link_libraries(fcitx5-ai
    Fcitx5::Core
    CURL::libcurl
)
target_include_directories(fcitx5-ai PRIVATE
    src/rapidjson-master/include
)

