cmake_minimum_required(VERSION 3.22)
project(31_Get_Video_PTS)

set(CMAKE_CXX_STANDARD 11)


# FFmpeg的安装目录，可以通过命令"brew info ffmpeg"获取
set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg@4/4.4.5)

# 头文件搜索路径
include_directories(${FFMPEG_DIR}/include/)
# 动态链接库或静态链接库的搜索路径
link_directories(${FFMPEG_DIR}/lib/)

add_executable(31_Get_Video_PTS main.cpp)


target_link_libraries(
        31_Get_Video_PTS
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)
