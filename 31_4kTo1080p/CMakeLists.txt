cmake_minimum_required(VERSION 3.22)
project(31_4kTo1080p)

set(CMAKE_CXX_STANDARD 11)


set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(31_4kTo1080p main.cpp convert_1.cpp convert_2.cpp)


target_link_libraries(
        31_4kTo1080p
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)
