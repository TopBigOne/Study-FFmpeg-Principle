#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/bprint.h>


#define VIDEO_PATH "/Users/dev/Desktop/yuv_data/juren-30s.mp4"
AVFormatContext *avFormatContext = NULL;
AVCodec         *avCodec         = NULL;
AVCodecContext  *avCodecContext  = NULL;

AVPacket *avPacket     = NULL;
AVFrame  *avFrame      = NULL;
AVFrame  *result_frame = NULL;

AVFilterInOut   *inputs;
AVFilterInOut   *outputs;
AVFilterGraph   *filter_graph;
AVFilterContext *main_src_ctx;
AVFilterContext *result_sink_ctx;


int result    = -1;
int err;
int read_end  = 0;
int frame_num = 0;


void free_all() {
    av_packet_free(&avPacket);
    av_frame_free(&avFrame);
    av_frame_free(&result_frame);
    avcodec_close(avCodecContext);
    avformat_free_context(avFormatContext);
    avfilter_graph_free(&filter_graph);
}


int initFFmpeg();

int init_filter();

int startDecode();

int isAudioIndex();

void free_all();

int save_yuv_to_file(AVFrame *tempFrame, int frame_num);

int save_yuv_to_file(AVFrame *tempFrame, int frame_num) {
    char yuv_pic_name[200] = {0};
    sprintf(yuv_pic_name,
            "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-3_scale/doc/yuv420p_%d.yuv", frame_num);
    FILE *fp = NULL;
    fp = fopen(yuv_pic_name, "wb+");
    if (fp == NULL) {
        puts("fopen in ERROR.\n");
        return -1;
    }
    fwrite(tempFrame->data[0], 1, tempFrame->linesize[0] * tempFrame->height, fp);
    fwrite(tempFrame->data[1], 1, tempFrame->linesize[1] * tempFrame->height / 2, fp);
    fwrite(tempFrame->data[2], 1, tempFrame->linesize[2] * tempFrame->height / 2, fp);
    fclose(fp);
    return 0;


}

int init_filter() {
    if (filter_graph != NULL) {
        return 0;
    }
    puts("init_filter");
    filter_graph   = avfilter_graph_alloc();
    if (!filter_graph) {
        fprintf(stderr, "avfilter_graph_alloc in ERROR.");
        return -1;
    }
    AVRational tb  = avFormatContext->streams[0]->time_base;
    // Guess the frame rate, based on both the container and codec information.
    AVRational fr  = av_guess_frame_rate(avFormatContext, avFormatContext->streams[0], NULL);
    AVRational sar = avFrame->sample_aspect_ratio;
    AVBPrint   args;
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args,
               "buffer=video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d[main];"
               "[main]scale=%d:%d[result];"
               "[result]buffersink",
               avFrame->width, avFrame->height, avFrame->format, tb.num, tb.den, sar.num, sar.den, fr.num, fr.den,
               avFrame->width / 2, avFrame->height / 2);

    // 解析滤镜字符串
    result = avfilter_graph_parse2(filter_graph, args.str, &inputs, &outputs);
    if (result < 0) {
        fprintf(stderr, "avfilter_graph_parse2 in ERROR.");
        return -1;
    }
    // 正式打开滤镜
    result = avfilter_graph_config(filter_graph, NULL);
    if (result < 0) {
        fprintf(stderr, "avfilter_graph_config in ERROR.");
        return -1;
    }

    // 根据名字找到 AVFilterContext
    main_src_ctx    = avfilter_graph_get_filter(filter_graph, "Parsed_buffer_0");
    result_sink_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffersink_2");
}


int main() {
    result = initFFmpeg();
    printf("main step 1 : result : %d\n", result);
    result = startDecode();
    printf("main step 2 : result : %d\n", result);
    free_all();
    return 0;
}

int initFFmpeg() {
    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        return -1;
    }
    result = avformat_open_input(&avFormatContext, VIDEO_PATH, NULL, NULL);
    if (result != 0) {
        return -1;
    }
    avCodecContext = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
    avCodec = avcodec_find_decoder(avCodecContext->codec_id);
    avcodec_open2(avCodecContext, avCodec, NULL);

    avPacket     = av_packet_alloc();
    avFrame      = av_frame_alloc();
    result_frame = av_frame_alloc();
    return 0;
}

int isAudioIndex() {
    return avPacket->stream_index == 1 ? 1 : 0;
}


int startDecode() {
    // 第一层 for ：
    // 主要是 :
    // step 1: read_frame();
    // step 2: send_packet()
    //      第二层 for :
    //      主要是 :
    //      step 1 : receive_frame()

    for (;;) {
        if (read_end == 1) {
            break;
        }
        result = av_read_frame(avFormatContext, avPacket);
        if (isAudioIndex()) {
            av_packet_unref(avPacket);
            continue;
        }
        if (result == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);

        } else {
            if (result != 0) {
                return ENOMEM;
            }
            TAG_RETRY:
            // 注意，这里容易出错。
            if (avcodec_send_packet(avCodecContext, avPacket) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
            av_packet_unref(avPacket);
        }


        // start decode the AVPacket
        for (;;) {
            result = avcodec_receive_frame(avCodecContext, avFrame);
            if(result<0){
              //  printf("the error is : %s \n", av_err2str(result));
            }

            printf(" start decode the AVPacket :result %d\n", result);
            if (result == AVERROR(EAGAIN)) {
                printf( "in AVERROR(EAGAIN)\n");
                break;
            }
            if (result == AVERROR_EOF) {
                read_end = 1;
                break;
            }
            if (result >= 0) {
                init_filter();
                result = av_buffersrc_add_frame_flags(main_src_ctx, avFrame, AV_BUFFERSRC_FLAG_PUSH);
                if (result < 0) {
                    printf("Error: av_buffersrc_add_frame failed\n");
                    return result;
                }
                // get the result frame
                result = av_buffersink_get_frame_flags(result_sink_ctx, result_frame, AV_BUFFERSRC_FLAG_PUSH);
                if (result >= 0) {
                    printf("save_yuv_to_file success\n");
                    save_yuv_to_file(result_frame, frame_num);
                    av_frame_unref(result_frame);
                    av_frame_unref(avFrame);
                } else {
                    fprintf(stderr, "av_buffersink_get_frame_flags in ERROR.");
                }
                frame_num++;
                if (frame_num > 10) {
                    return 666;
                }
                continue;

            }

            printf("other failed.\n");
        }

    }

    return result;


}

