#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/bprint.h>

AVFormatContext *avFormatContext = NULL;
AVCodecContext  *avCodecContext  = NULL;
AVCodec         *avCodec         = NULL;
AVFrame         *avFrame         = NULL;
AVFrame         *result_frame    = NULL;
AVPacket        *avPacket        = NULL;

AVFilterGraph   *avFilterGraph   = NULL;
AVFilterContext *main_src_ctx    = NULL;
AVFilterContext *split_ctx       = NULL;
AVFilterContext *scale_ctx       = NULL;
AVFilterContext *overlay_ctx     = NULL;
AVFilterContext *result_sink_ctx = NULL;


int result   = -1;
int read_end = 0;


int InitFFmpeg();

int InitFilter();

int startDecoder();

void FreeAll();

int isAudioIndex(AVPacket *packet);

void SaveYuvToFile(AVFrame *pFrame, int num);

int isAudioIndex(AVPacket *packet) {
    return packet->stream_index == 1 ? 1 : 0;
}

int InitFFmpeg() {
    avFormatContext        = avformat_alloc_context();
    if (!avFormatContext) {
        return -1;
    }
    const char *video_Path = "/Users/dev/Desktop/yuv_data/juren-30s.mp4";
    result = avformat_open_input(&avFormatContext, video_Path, NULL, NULL);
    if (result != 0) {
        return result;
    }
    avCodecContext = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
    avCodec = avcodec_find_decoder(avCodecContext->codec_id);
    result  = avcodec_open2(avCodecContext, avCodec, NULL);
    if (result < 0) {
        return result;
    }

    avFrame      = av_frame_alloc();
    result_frame = av_frame_alloc();
    avPacket     = av_packet_alloc();
}

int frame_num = 0;

int InitFilter() {
    if (avFilterGraph != NULL) {
        return 0;
    }

    avFilterGraph = avfilter_graph_alloc();
    if (!avFilterGraph) {
        return -1;
    }
    AVRational tb  = avFormatContext->streams[0]->time_base;
    AVRational fr  = av_guess_frame_rate(avFormatContext, avFormatContext->streams[0], NULL);
    AVRational sar = avFrame->sample_aspect_ratio;
    AVBPrint   args;
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args,
               "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
               avFrame->width, avFrame->height, avFrame->format,
               tb.num, tb.den,
               sar.num, sar.den,
               fr.num, fr.den);


    // 创建buffer 滤镜
    // [buffer filter]------->[功能性滤镜]------>[sink filter]
    result = avfilter_graph_create_filter(&main_src_ctx, avfilter_get_by_name("buffer"),
                                          "Parsed_buffer_0_666", args.str, NULL, avFilterGraph);
    if (result < 0) {
        fprintf(stderr, "create buffer filter in ERROR: %s\n", av_err2str(result));
        return result;
    }

    // 创建 buffer sink 滤镜 : 没有参数
    result = avfilter_graph_create_filter(&result_sink_ctx, avfilter_get_by_name("buffersink"), "Parsed_1_888", NULL,
                                          NULL, avFilterGraph);

    if (result < 0) {
        fprintf(stderr, "create buffer_sink filter in ERROR: %s\n", av_err2str(result));
        return result;
    }
    // 创建split 滤镜ctx；参数：outputs=2
    result = avfilter_graph_create_filter(&split_ctx, avfilter_get_by_name("split"), "Parsed_split_2_777", "outputs=2",
                                          NULL, avFilterGraph);
    if (result < 0) {
        fprintf(stderr, "create split split in ERROR: %s\n", av_err2str(result));
        return result;
    }

    // 连接buffer 滤镜跟split 滤镜的第一个输出流
    if ((result = avfilter_link(main_src_ctx, 0, split_ctx, 0)) < 0) {
        fprintf(stderr, "filter link main_src_ctx to split_ctx in ERROR: %s", av_err2str(result));
        return result;
    }

    // 创建scale 滤镜ctx
    av_bprint_clear(&args);
    av_bprintf(&args, "%d:%d", avFrame->width / 4, avFrame->height / 4);

    result = avfilter_graph_create_filter(&scale_ctx, avfilter_get_by_name("scale"),
                                          "Parsed_scale_3_444", args.str, NULL, avFilterGraph);
    if (result < 0) {
        fprintf(stderr, "create  scale filter in ERROR: %s", av_err2str(result));
        return result;
    }

    // 连接split 滤镜的第一个输入流到scale滤镜的第一个输入流
    if ((result = avfilter_link(split_ctx, 0, scale_ctx, 0)) < 0) {
        fprintf(stderr, "filter link split_ctx to scale_ctx in ERROR: %s\n", av_err2str(result));
        return result;
    }


    // 创建overlay 滤镜
    av_bprint_clear(&args);
    av_bprintf(&args, "%d*%d", avFrame->width / 4 * 3, avFrame->height / 4 * 3);
    result = avfilter_graph_create_filter(&overlay_ctx, avfilter_get_by_name("overlay"), "Parsed_scale_4_444", args.str,
                                          NULL, avFilterGraph);

    if (result < 0) {
        fprintf(stderr, "create overlay filter in ERROR: %s\n", av_err2str(result));
        return result;
    }

    if ((result = avfilter_link(scale_ctx, 0, overlay_ctx, 1)) < 0) {
        fprintf(stderr, "filter link scale_ctx to overlay_ctx in ERROR: %s\n", av_err2str(result));
        return result;

    }
    // 连接split 滤镜的第一个输入流到scale滤镜的第一个输入流
    if ((result = avfilter_link(split_ctx, 1, overlay_ctx, 0)) < 0) {
        fprintf(stderr, "filter link split_ctx to overlay_ctx in ERROR: %s\n", av_err2str(result));
        return result;
    }

    // 连接overlay_ctx 滤镜的第一个输出流到resultsink_ctx滤镜的第一个输入流
    if ((result = avfilter_link(overlay_ctx, 0, result_sink_ctx, 0)) < 0) {
        fprintf(stderr, "filter link overlay_ctx to result_sink_ctx in ERROR: %s\n", av_err2str(result));
        return result;
    }

    // 正式打开滤镜
    result = avfilter_graph_config(avFilterGraph, NULL);
    if (result < 0) {
        fprintf(stderr, "avfilter_graph_config in ERROR: %s\n", av_err2str(result));
        return result;
    }
    return result;
}

int startDecoder() {
    for (;;) {
        if (read_end == 1) {
            break;
        }
        result = av_read_frame(avFormatContext, avPacket);
        if (isAudioIndex(avPacket)) {
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
            if (avcodec_send_packet(avCodecContext, avPacket) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
            av_packet_unref(avPacket);
        }

        for (;;) {
            result = avcodec_receive_frame(avCodecContext, avFrame);
            if (result == AVERROR_EOF) {
                read_end = 1;
                break;
            }

            if (result == AVERROR(EAGAIN)) {
                break;
            }

            if (result >= 0) {
                result = InitFilter();
                printf("InitFilter result %d\n", result);

                result = av_buffersrc_add_frame_flags(main_src_ctx, avFrame, AV_BUFFERSRC_FLAG_PUSH);
                if (result < 0) {
                    fprintf(stderr, "add_frame_flags in ERROR: %s", av_err2str(result));
                    return result;
                }

                result = av_buffersink_get_frame_flags(result_sink_ctx, result_frame, AV_BUFFERSRC_FLAG_PUSH);
                if (result >= 0) {
                    printf("save_yuv_to_file success.\n");
                    SaveYuvToFile(result_frame, frame_num);
                } else {
                    fprintf(stderr, "buffersink_get_frame_flags in ERROR: %s", av_err2str(result));
                }
                frame_num++;
                if (frame_num > 10) {
                    return 666;
                }
                continue;

            }
            printf("other error: %d", result);
            break;
        }

    }
    return result;

}

void SaveYuvToFile(AVFrame *pFrame, int num) {
    char fileName[200] = {0};
    sprintf(fileName, "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/11-2_split/doc/yuv_420p_%d.yuv",
            num);
    FILE *fp = NULL;
    fp = fopen(fileName, "wb+");
    if (fp == NULL) {
        fprintf(stderr, " fopen in ERROR\n");
    }

    fwrite(pFrame->data[0], 1, pFrame->linesize[0] * pFrame->height, fp);
    fwrite(pFrame->data[1], 1, pFrame->linesize[1] * pFrame->height / 2, fp);
    fwrite(pFrame->data[2], 1, pFrame->linesize[2] * pFrame->height / 2, fp);

    fclose(fp);


}

void FreeAll() {
    av_frame_free(&avFrame);
    av_frame_free(&result_frame);
    av_packet_free(&avPacket);
    avcodec_close(avCodecContext);
    avformat_free_context(avFormatContext);
    avfilter_free(main_src_ctx);
    avfilter_free(split_ctx);
    avfilter_free(scale_ctx);
    avfilter_free(overlay_ctx);
    avfilter_free(result_sink_ctx);
    avfilter_graph_free(&avFilterGraph);
}

int main() {
    result = InitFFmpeg();
    printf("InitFFmpeg result   : %d\n", result);
    result = startDecoder();
    printf("startDecoder result : %d\n", result);
    FreeAll();
    return 0;
}
