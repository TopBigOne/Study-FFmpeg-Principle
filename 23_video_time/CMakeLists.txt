cmake_minimum_required(VERSION 3.22)
project(23_video_time C)

set(CMAKE_C_STANDARD 99)



set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(23_video_time main.c)

target_link_libraries(
        23_video_time
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)


