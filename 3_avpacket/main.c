#include <stdio.h>

#include <libavformat/avformat.h>

#define  video_path  "/Users/dev/Desktop/mp4/貳佰《玫瑰》1080P.mp4"

int main() {
    AVFormatContext *avFormatContext = NULL;

    int type = 2;
    int err;
    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        //  Cannot allocate memory
        printf("error code is : %d\n", AVERROR(ENOMEM));
        return ENOMEM;

    }
    if ((err = avformat_open_input(&avFormatContext, video_path, NULL, NULL)) < 0) {
        printf("error code : %d\n", err);
        return err;
    }
    if (type == 1) {
        AVPacket *avPacket = av_packet_alloc();
        int ret = 0;
        ret = av_read_frame(avFormatContext, avPacket);
        if (ret < 0) {
            fprintf(stderr, "read av packet in ERROR\n");
            return ret;
        }
        printf("read av packet in SUCCESS\n");
        printf("stream 0 type : %d \n", avFormatContext->streams[0]->codecpar->codec_type);
        printf("stream 1 type : %d \n", avFormatContext->streams[1]->codecpar->codec_type);
        printf("stream_index  : %d\n", avPacket->stream_index);
        int64_t duration = avPacket->duration;
        int time_base = avFormatContext->streams[1]->time_base.num / avFormatContext->streams[0]->time_base.den;
        printf("duration  : %lld\n", duration);
        printf("time_base : %d\n", time_base);
        printf("size      : %d\n", avPacket->size);
        printf("pos       : %lld\n", avPacket->pos);
        printf("data[0]   : %x\n", avPacket->data[0]);
        printf("data[1]   : %x\n", avPacket->data[1]);
        printf("data[2]   : %x\n", avPacket->data[2]);
        printf("data[3]   : %x\n", avPacket->data[3]);
        printf("data[4]   : %x\n", avPacket->data[4]);
        printf("data[5]   : %x\n", avPacket->data[5]);
        printf("data[6]   : %x\n", avPacket->data[6]);
        av_packet_free(&avPacket);
        return 0;

    }
    if (type == 2) {
        AVPacket *avPacket = av_packet_alloc();
        int ret, i = 0;
        for (i = 0; i < 100; i++) {
            ret = av_read_frame(avFormatContext, avPacket);
            if (ret < 0) {
                fprintf(stderr, "read fail.\n");
                return ret;
            }
            printf("read av packet in success\n");
            printf("stream 0 type : %d\n", avFormatContext->streams[0]->codecpar->codec_type);
            printf("stream 0 type : %d\n", avFormatContext->streams[1]->codecpar->codec_type);
            printf("stream index  : %d\n", avPacket->stream_index);
            int64_t duration = avPacket->duration;
            int time_base = avFormatContext->streams[1]->time_base.num / avFormatContext->streams[0]->time_base.den;

            printf("duration      : %lld\n", duration);
            printf("time_base     : %d\n", time_base);
            printf("size          : %d\n", avPacket->size);
            printf("pos           : %lld\n", avPacket->pos);
            printf("data[0]   : %x\n", avPacket->data[0]);
            printf("data[1]   : %x\n", avPacket->data[1]);
            printf("data[2]   : %x\n", avPacket->data[2]);
            printf("data[3]   : %x\n", avPacket->data[3]);
            printf("data[4]   : %x\n", avPacket->data[4]);
            printf("data[5]   : %x\n", avPacket->data[5]);
            printf("data[6]   : %x\n", avPacket->data[6]);
            av_packet_free(&avPacket);
        }
    }
    return 0;
}
