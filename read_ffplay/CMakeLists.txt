cmake_minimum_required(VERSION 3.22)
project(read_ffplay C)

set(CMAKE_C_STANDARD 99)

# FFmpeg
set(ffmpeg_prefix "/usr/local/ffmpeg/4.4")
set(ffmpeg_exec_prefix "${ffmpeg_prefix}")
set(libdir "${ffmpeg_exec_prefix}/lib")
set(FFMPEG_PREFIX "/usr/local/Cellar/ffmpeg/4.1.3")
set(FFMPEG_EXEC_PREFIX "/usr/local/Cellar/ffmpeg/4.1.3")
set(FFMPEG_LIBDIR "${ffmpeg_exec_prefix}/lib")
set(FFMPEG_INCLUDE_DIRS "${ffmpeg_prefix}/include/")


# SDL2
set(prefix "/opt/homebrew/Cellar/sdl2/2.0.18")
set(exec_prefix "${prefix}")
set(libdir "${exec_prefix}/lib")
set(SDL2_PREFIX "/usr/local/Cellar/sdl2/2.0.9_1")
set(SDL2_EXEC_PREFIX "/usr/local/Cellar/sdl2/2.0.9_1")
set(SDL2_LIBDIR "${exec_prefix}/lib")
set(SDL2_INCLUDE_DIRS "${prefix}/include/SDL2")
set(SDL2_LIBRARIES "-L${SDL2_LIBDIR}  -lSDL2")
string(STRIP "${SDL2_LIBRARIES}" SDL2_LIBRARIES)

include_directories(
        ${FFMPEG_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
)


link_directories(
        ${FFMPEG_LIBDIR}
        ${SDL2_LIBDIR}
)
set(SOURCE_FILES
        config.h
        cmdutils.h
        cmdutils.c
        ffplay.c
        )


add_executable(read_ffplay
        ${SOURCE_FILES})

target_link_libraries(
        read_ffplay
        swscale
        swresample
        avcodec
        avutil
        avdevice
        avfilter
        avformat
        SDL2
)

