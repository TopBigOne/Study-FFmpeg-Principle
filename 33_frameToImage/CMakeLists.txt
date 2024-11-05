cmake_minimum_required(VERSION 3.22)
project(33_frameToImage C)

set(CMAKE_C_STANDARD 99)




set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg@4/4.4.5)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(33_frameToImage main.c)

target_link_libraries(
        33_frameToImage
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)

