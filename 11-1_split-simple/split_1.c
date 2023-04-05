//
// Created by dev on 2023/3/30.
//

#include "split_1.h"


int InitFFmpeg() {
    avFormatContext        = avformat_alloc_context();
    const char *video_path = "/Users/dev/Desktop/yuv_data/juren-30s.mp4";
    RESULT = avformat_open_input(&avFormatContext, video_path, NULL, NULL);
    if (RESULT != 0) {
        return RESULT;
    }
    avCodecContext = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
    avCodec = avcodec_find_decoder(avCodecContext->codec_id);
    avcodec_open2(avCodecContext, avCodec, NULL);

    avPacket     = av_packet_alloc();
    avFrame      = av_frame_alloc();
    result_frame = av_frame_alloc();

}

int checkAudioIndex(AVPacket *packet) {
    return packet->stream_index == 1 ? 1 : 0;
}


int StartDecoder() {
    for (;;) {
        if (READ_END == 1) {
            break;
        }

        RESULT = av_read_frame(avFormatContext, avPacket);
        if (checkAudioIndex(avPacket)) {
            av_packet_unref(avPacket);
            continue;
        }
        if (RESULT == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);

        } else {
            if (RESULT != 0) {
                return ENOMEM;
            }
            TAG_RETRY:
            if (avcodec_send_packet(avCodecContext, avPacket) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
        }

        for (;;) {
            RESULT = avcodec_receive_frame(avCodecContext, avFrame);
            if (RESULT == AVERROR(EAGAIN)) {
                break;
            }
            if (RESULT == AVERROR_EOF) {
                READ_END = 1;
                break;
            }
            if (RESULT >= 0) {
                InitFilter();

                // 往 滤镜上下文 发送一个 AVFrame，让滤镜进行处理。
                RESULT = av_buffersrc_add_frame_flags(main_src_ctx, avFrame, AV_BUFFERSRC_FLAG_PUSH);
                if (RESULT < 0) {
                    printf("Error: av_buffersrc_add_frame failed\n");
                    return RESULT;
                }
                RESULT = av_buffersink_get_frame_flags(result_sink_ctx, result_frame, AV_BUFFERSRC_FLAG_PUSH);

                if (RESULT >= 0) {
                    save_yuv_to_file(result_frame, FRAME_COUNT);
                }
                FRAME_COUNT++;
                if (FRAME_COUNT > 10) {
                    return 666;
                }
                continue;
            }
            printf("other fail \n");
            return RESULT;

        }

    }
    return RESULT;
}

int InitFilter() {
    AVFilterInOut *inputs;
    AVFilterInOut *outputs;
    if (filter_graph != NULL) {
        return 0;
    }
    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        return -1;
    }
    AVRational tb  = avFormatContext->streams[0]->time_base;
    AVRational fr  = av_guess_frame_rate(avFormatContext, avFormatContext->streams[0], NULL);
    AVRational sar = avFrame->sample_aspect_ratio;

    AVBPrint args;
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args, "buffer=video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d[main];"
                      "[main]split[v0][v1];"
                      "[v0]scale=%d:%d[v2];"
                      "[v1][v2]overlay=%d:%d[RESULT];"
                      "[RESULT]buffersink",
               avFrame->width, avFrame->height, avFrame->format, tb.num, tb.den, sar.num, sar.den, fr.num, fr.den,
               avFrame->width / 4, avFrame->height / 4,
               avFrame->width / 4 * 3, avFrame->height / 4 * 3);// overlay 的位置
    // 解析滤镜字符串
    RESULT = avfilter_graph_parse2(filter_graph, args.str, &inputs, &outputs);
    if (RESULT < 0) {
        printf("Cannot configure graph\n");
        return RESULT;
    }
    // 正式打开滤镜
    RESULT = avfilter_graph_config(filter_graph, NULL);
    if (RESULT < 0) {
        printf("Cannot configure graph\n");
        return RESULT;
    }

    main_src_ctx    = avfilter_graph_get_filter(filter_graph, "Parsed_buffer_0");
    result_sink_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffersink_4");
    return RESULT;
}


void save_yuv_to_file(AVFrame *tempAVFrame, int count) {
    AVBPrint f_name;
    av_bprint_init(&f_name, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&f_name,
               "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/11-1_split-simple/doc/yuv420p_%d.yuv",
               FRAME_COUNT);
    FILE *fp = NULL;
    fp = fopen(f_name.str, "wb+");
    fwrite(tempAVFrame->data[0], 1, tempAVFrame->linesize[0] * tempAVFrame->height, fp);
    fwrite(tempAVFrame->data[1], 1, tempAVFrame->linesize[1] * tempAVFrame->height / 2, fp);
    fwrite(tempAVFrame->data[2], 1, tempAVFrame->linesize[2] * tempAVFrame->height / 2, fp);

}

int FreeAll() {
    av_frame_free(&result_frame);
    av_frame_free(&avFrame);
    av_packet_free(&avPacket);
    avcodec_close(avCodecContext);
    avformat_free_context(avFormatContext);
    avfilter_graph_free(&filter_graph);
}

int start_task() {
    InitFFmpeg();
    StartDecoder();
    FreeAll();
}
