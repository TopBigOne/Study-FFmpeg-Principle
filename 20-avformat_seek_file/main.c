#include <stdio.h>
#include <libavformat/avformat.h>

void my_code();

int read_packet(AVFormatContext *pContext);

int main() {

    my_code();

    return 0;
}

void my_code() {
    int             err, ret;
    AVFormatContext *avFormatContext = NULL;
    char            *file_name       = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/20-avformat_seek_file/juren-30s.mp4";
    avFormatContext = avformat_alloc_context();
    avformat_open_input(&avFormatContext, file_name, NULL, NULL);

    read_packet(avFormatContext);


    // 按照时间 seek ，往前跳转到 5s的位置
    int64_t seek_target = 5 * AV_TIME_BASE;
    int64_t seek_min    = INT64_MIN;
    int64_t seek_max    = INT64_MAX;

    ret = avformat_seek_file(avFormatContext, -1,
                             seek_min, seek_target, seek_max, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s: error while seeking\n", avFormatContext->url);
    }

    printf("                \n");
    printf("AFTER SEEK FILE:\n");
    read_packet(avFormatContext);
    read_packet(avFormatContext);
    read_packet(avFormatContext);


}

int read_packet(AVFormatContext *pContext) {
    AVPacket *packet = av_packet_alloc();
    int      ret     = 0;
    ret = av_read_frame(pContext, packet);
    if (ret < 0) {
        return ret;
    }
    int     stream_index = packet->stream_index;
    int64_t pts          = packet->pts;
    int64_t pos          = packet->pos;
    printf("-----------------------------------↓\n");
    printf("stream_index : %d\n", stream_index);
    printf("pts          : %lld\n", pts);
    printf("pos          : %lld\n", pos);


    printf("data : %x %x %x %x %x %x %x %x \n",
           packet->data[0],packet->data[1], packet->data[2],
           packet->data[3], packet->data[4],packet->data[5],
           packet->data[6],packet->data[7]);
    printf("-----------------------------------↑\n");


    av_packet_unref(packet);
    av_packet_free(&packet);
    return ret;
}
