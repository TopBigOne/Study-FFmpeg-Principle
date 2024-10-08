#include <stdio.h>

#include <libavformat/avformat.h>

int main() {
    void                *ifmt_opaque = NULL;
    const AVInputFormat *ifmt        = NULL;
    // ffmpeg 支持的解 封装格式：
    while ((ifmt = av_demuxer_iterate(&ifmt_opaque))) {
        printf("ifmt name is : %s \n", ifmt->name);
    }

    printf("----------------------------------------|\n");
    // ffmpeg 支持的封装格式
    void                 *ofmt_opaque = NULL;
    const AVOutputFormat *ofmt        = NULL;
    while ((ofmt = av_muxer_iterate(&ofmt_opaque))) {
        printf("ofmt name is : %s\n", ofmt->name);

    }
    return 0;
}
