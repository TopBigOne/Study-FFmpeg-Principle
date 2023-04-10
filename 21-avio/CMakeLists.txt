cmake_minimum_required(VERSION 3.22)
project(21_avio C)

set(CMAKE_C_STANDARD 99)

set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(21_avio main.c main_write.c)

target_link_libraries(
        21_avio
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)



