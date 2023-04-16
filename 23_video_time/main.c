#include <stdio.h>

#include <libavformat/avformat.h>

#define VIDEO_PATH "/Users/dev/Desktop/mp4/孤城 洛先生 『月照入心頭世間的愛恨情仇』.mp4"
#define video_path "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/17-format_filter/video/juren-30s.mp4"


int result   = -1;
int read_end = 0;

void init_ffmpeg();

void start_decoder();

int main() {
    init_ffmpeg();
    start_decoder();
    return 0;
}

AVCodecContext  *avCodecContext  = NULL;
AVFormatContext *avFormatContext = NULL;
AVPacket        *avPacket        = NULL;
AVFrame         *avFrame         = NULL;


void init_ffmpeg() {
    avFormatContext = avformat_alloc_context();
    result          = avformat_open_input(&avFormatContext, video_path, NULL, NULL);
    if (result != 0) {
        perror("avformat_open_input in ERROR.");
        return;
    }
    avCodecContext = avcodec_alloc_context3(NULL);
    result         = avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
    if (result < 0) {
        perror("avcodec_parameters_to_context in ERROR.");
    }

    AVCodec *avCodec = avcodec_find_decoder(avCodecContext->codec_id);

    result = avcodec_open2(avCodecContext, avCodec, NULL);
    if (result != 0) {
        perror("avformat_open_input in ERROR.");
        return;
    }

    avPacket = av_packet_alloc();
    avFrame  = av_frame_alloc();
}


void start_decoder() {
    AVStream *video_stream = avFormatContext->streams[0];
    int64_t  duration      = video_stream->duration;
    int64_t  second_times  = duration * av_q2d(video_stream->time_base);
    printf("这个视频的时长: %lld s.\n", second_times);

    for (;;) {
        if (read_end == 1) {
            break;

        }
        result = av_read_frame(avFormatContext, avPacket);
        if (avPacket->stream_index == 1) {
            av_packet_unref(avPacket);
            continue;
        }

        if (result == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);
        } else {
            if (result != 0) {
                return;
            }
            TAG_RETRY:
            if ((avcodec_send_packet(avCodecContext, avPacket)) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
            double play_time_stamp = avPacket->pts * av_q2d(video_stream->time_base);
            printf("play_time_stamp : %f\n", play_time_stamp);

            // 解除avpacket的 引用；
            av_packet_unref(avPacket);
        }

        for (;;) {
            result = avcodec_receive_frame(avCodecContext, avFrame);
            if (result == AVERROR_EOF) {
                read_end = 1;
                break;
            }
            if (result == AVERROR(AVERROR(EAGAIN))) {
                break;
            }
            if (result == 0) {
                puts("收到完整的frame");
                continue;
            }
            fprintf(stderr, "avcodec_receive_frame in ERROR : %s\n", av_err2str(result));
            break;
        }

    }


}
