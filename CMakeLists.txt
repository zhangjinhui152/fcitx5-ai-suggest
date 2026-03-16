cmake_minimum_required(VERSION 3.10.0)
project(fcitxAddon VERSION 0.1.0 LANGUAGES C CXX)
project(fcitx5-ai)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Fcitx5Core REQUIRED)

add_library(fcitx5-ai MODULE
    src/ai.cpp
)

target_link_libraries(fcitx5-ai
    Fcitx5::Core
)
 