cmake_minimum_required(VERSION 3.22)
project(21_avio_write_luo C)

set(CMAKE_C_STANDARD 99)


set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg@4/4.4.5)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(21_avio_write_luo main.c)

target_link_libraries(
        21_avio_write_luo
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)








