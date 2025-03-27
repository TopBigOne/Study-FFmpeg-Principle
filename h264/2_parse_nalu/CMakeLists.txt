cmake_minimum_required(VERSION 3.22)
project(2_parse_nalu C)

set(CMAKE_C_STANDARD 99)

add_executable(2_parse_nalu main.c
        h264_parser.h
        h264_parser.c
        )
