cmake_minimum_required(VERSION 3.22)
project(8_av_log C)

set(CMAKE_C_STANDARD 99)


set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)
add_executable(8_av_log main.c)
target_link_libraries(
        8_av_log
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)

