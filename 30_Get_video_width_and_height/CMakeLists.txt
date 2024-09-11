cmake_minimum_required(VERSION 3.22)
project(30_Get_video_width_and_height)

set(CMAKE_CXX_STANDARD 11)

# FFmpeg的安装目录，可以通过命令"brew info ffmpeg"获取
set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg@4/4.4.5)

# 头文件搜索路径
include_directories(${FFMPEG_DIR}/include/)
# 动态链接库或静态链接库的搜索路径
link_directories(${FFMPEG_DIR}/lib/)

add_executable(30_Get_video_width_and_height main.cpp)


target_link_libraries(
        30_Get_video_width_and_height
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)
