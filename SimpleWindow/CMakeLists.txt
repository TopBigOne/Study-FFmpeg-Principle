cmake_minimum_required(VERSION 3.22)
project(SimpleWindow C)

set(CMAKE_C_STANDARD 99)


set(SDL2_DIR /opt/homebrew/Cellar/sdl2/2.0.18/)
include_directories(${SDL2_DIR}/include/)
link_directories(${SDL2_DIR}/lib/)

add_executable(SimpleWindow main.c)

target_link_libraries(
        SimpleWindow
        SDL2
#         SLD2_test
        SDL2main
        )

