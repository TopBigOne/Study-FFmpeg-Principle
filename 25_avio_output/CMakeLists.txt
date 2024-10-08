cmake_minimum_required(VERSION 3.22)
project(25_avio_output C)

set(CMAKE_C_STANDARD 99)






set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg@4/4.4.5)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(25_avio_output main.c)

target_link_libraries(
        25_avio_output
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)




