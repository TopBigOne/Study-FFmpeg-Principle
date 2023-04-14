cmake_minimum_required(VERSION 3.22)
project(read_ffplay C)

set(CMAKE_C_STANDARD 99)

set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(read_ffplay
        ffmpeg.c ffmpeg.h
        cmdutils.h cmdutils.c
        sync_queue.h sync_queue.c
        ffmpeg_opt.c
        config.h
        fopen_utf8.h
        opt_common.h opt_common.c
        config_components.h
        my_fifo.h my_fifo.c
        error.h error.c
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

