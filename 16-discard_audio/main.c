#include <stdio.h>

#include <libavformat/avformat.h>

int my_code() {
    int             ret              = 0;
    const char      *file_path       = "/Users/dev/Desktop/yuv_data/juren-30s.mp4";
    AVFormatContext *avFormatContext = avformat_alloc_context();
    ret = avformat_open_input(&avFormatContext, file_path, NULL, NULL);
    printf("stream 0 type: %d\n", avFormatContext->streams[0]->codecpar->codec_type);
    printf("stream 1 type: %d\n", avFormatContext->streams[1]->codecpar->codec_type);
    printf("--------------------------------------------------------------");

    avFormatContext->streams[1]->discard = AVDISCARD_ALL;
    AVPacket *avPacket = av_packet_alloc();
    for (int i         = 0; i < 100; ++i) {
        ret = av_read_frame(avFormatContext, avPacket);
        printf("stream index: %d\n", avPacket->stream_index);
    }
    return ret;


}

int main() {
    my_code();


    return 0;
}
