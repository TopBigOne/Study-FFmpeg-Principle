cmake_minimum_required(VERSION 3.22)
project(3_avpacket C)

set(CMAKE_C_STANDARD 99)




# FFmpeg的安装目录，可以通过命令"brew info ffmpeg"获取
set(FFMPEG_DIR /opt/homebrew/Cellar/ffmpeg/4.4.1_4)

# 头文件搜索路径
include_directories(${FFMPEG_DIR}/include/)
# 动态链接库或静态链接库的搜索路径
link_directories(${FFMPEG_DIR}/lib/)

add_executable(3_avpacket main.c)


target_link_libraries(
        3_avpacket
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
)


