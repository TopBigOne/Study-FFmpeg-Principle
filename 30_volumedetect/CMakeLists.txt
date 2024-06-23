cmake_minimum_required(VERSION 3.22)
project(volumedetect)

set(CMAKE_CXX_STANDARD 11)





set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)
include_directories(${FFMPEG_DIR}/include/)
link_directories(${FFMPEG_DIR}/lib)

add_executable(volumedetect main.cpp)


target_link_libraries(
        volumedetect
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)

