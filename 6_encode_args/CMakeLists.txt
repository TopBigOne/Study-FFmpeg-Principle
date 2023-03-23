cmake_minimum_required(VERSION 3.22)
project(6_encode_args C)

set(CMAKE_C_STANDARD 99)



set(CMAKE_C_STANDARD 99)
set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)


add_executable(6_encode_args main.c)

target_link_libraries(
        6_encode_args
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)

