cmake_minimum_required(VERSION 3.22)
project(read_ffplay C)

set(CMAKE_C_STANDARD 99)



set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(read_ffplay
        read_ffplay.c read_ffplay.h
        ffmpeg.c ffmpeg.h
        cmdutils.h cmdutils.c
        sync_queue.h sync_queue.c
        )

target_link_libraries(
        read_ffplay
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)

