cmake_minimum_required(VERSION 3.22)
project(5_encode C)

set(CMAKE_C_STANDARD 99)
set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(5_encode main.c)

target_link_libraries(
        5_encode
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)
