cmake_minimum_required(VERSION 3.22)
project(11_1_split_simple C)

set(CMAKE_C_STANDARD 99)


set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)

include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)


add_executable(11_1_split_simple
        main.c
        file_path.h
        split_2.h
        split_2.c
        #        split_1.c
        #        split_1.h
        )


target_link_libraries(
        11_1_split_simple
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)
