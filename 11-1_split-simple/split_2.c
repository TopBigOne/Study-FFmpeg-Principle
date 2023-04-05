//
// Created by dev on 2023/3/30.
//

#include "split_2.h"

#include "file_path.h"


int init_ffmpeg() {
    av_FormatContext = avformat_alloc_context();
    if (!av_FormatContext) {
        return -1;
    }


    RESULT = avformat_open_input(&av_FormatContext, VIDEO_URI, NULL, NULL);
    check_result(RESULT, "avformat_open_input")

    avCodecContext = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(avCodecContext, av_FormatContext->streams[0]->codecpar);
    avCodec = avcodec_find_decoder(avCodecContext->codec_id);

    RESULT = avcodec_open2(avCodecContext, avCodec, NULL);
    check_result(RESULT, "avcodec_open2")
    return RESULT;

}

int is_audio_index(AVPacket *packet) {
    return packet->stream_index == 1 ? 1 : 0;

}

int start_decoder() {
    for (;;) {
        // case 1:
        if (READ_END == 1) {
            break;
        }

        // case 2：
        RESULT = av_read_frame(av_FormatContext, avPacket);
        if (is_audio_index(avPacket)) {
            av_packet_unref(avPacket);
            continue;
        }
        // case 3：
        if (RESULT == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);
        } else {
            if (RESULT < 0) {
                break;
            }
            TAG_RETRY:
            if (avcodec_send_packet(avCodecContext, avPacket) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
            av_packet_unref(avPacket);
        }
        // step 2: process the avframe
        for (;;) {
            RESULT = avcodec_receive_frame(avCodecContext, avFrame);
            if (RESULT == AVERROR_EOF) {
                READ_END = 1;
                break;
            }
            if (RESULT == AVERROR(EAGAIN)) {
                break;
            }
            if (RESULT >= 0) {
                // core case :
                RESULT = init_filter();
                RESULT = av_buffersrc_add_frame_flags(main_src_Context, avFrame, AV_BUFFERSRC_FLAG_PUSH);
                check_result(RESULT, "buffersrc add frame")

                RESULT = av_buffersink_get_frame_flags(result_sink_Context, resultFrame, AV_BUFFERSRC_FLAG_PUSH);
                check_result(RESULT, "buffersink get frame")
                printf("save yuv to file\n");
                save_yuv_to_file(avFrame, FRAME_COUNT);
                FRAME_COUNT++;
                if (FRAME_COUNT > 10) {
                    return 666;
                }


                continue;
            }
            check_result(RESULT, "other error.")

        }

    }
    return RESULT;

}

int init_filter() {
    if (avFilterGraph != NULL) {
        return 0;
    }
    AVRational tb  = av_FormatContext->streams[0]->time_base;
    AVRational fr  = av_guess_frame_rate(av_FormatContext, av_FormatContext->streams[0], NULL);
    AVRational sar = avFrame->sample_aspect_ratio;
    AVBPrint   args;
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args, "video_size =%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
               avFrame->width, avFrame->height, avFrame->format,
               tb.num, tb.den,
               sar.num, sar.den,
               fr.num, fr.den);
    // create buffer filter
    RESULT = avfilter_graph_create_filter(&main_src_Context, avfilter_get_by_name("buffer"), "Parsed_0_666", args.str,
                                          NULL, avFilterGraph);
    check_result(RESULT, "create buffer filter")
    // create the buffersink filter;
    RESULT = avfilter_graph_create_filter(&result_sink_Context, avfilter_get_by_name("buffersink"), "Parsed_1_888",
                                          NULL, NULL, avFilterGraph);
    check_result(RESULT, "create buffersink filter")

    // 创建splint 滤镜
    RESULT = avfilter_graph_create_filter(&split_Context, avfilter_get_by_name("split"), "Parsed_split_2_777",
                                          "outputs=2", NULL, avFilterGraph);
    check_result(RESULT, "create split filter")
    // link filter
    RESULT = avfilter_link(main_src_Context, 0, split_Context, 1);
    check_result(RESULT, "link main_src to split ")

    // 创建scale filer
    av_bprint_clear(&args);
    av_bprintf(&args, "%d:%d", avFrame->width / 4, avFrame->height / 4);
    RESULT = avfilter_graph_create_filter(&scale_Context, avfilter_get_by_name("scale"), "Parsed_scale_3_444", args.str,
                                          NULL, avFilterGraph);
    check_result(RESULT, "create scale filter")

    // link split to scale
    RESULT = avfilter_link(split_Context, 0, scale_Context, 0);
    check_result(RESULT, "link split to scale")

    // create overlay filter
    av_bprint_clear(&args);
    av_bprintf(&args, "%d:%d", avFrame->width / 4 * 3, avFrame->height / 4 * 3);
    RESULT = avfilter_graph_create_filter(&overlay_Context, avfilter_get_by_name("overlay"), "Parsed_4_444", args.str,
                                          NULL, avFilterGraph);
    check_result(RESULT, " create overlay filter")

    // link scale index 0  to overlay index 1;
    RESULT = avfilter_link(scale_Context, 0, overlay_Context, 1);
    check_result(RESULT, "link scale index 0  to overlay index 1")

    // link split 1 to overlay index 0;
    RESULT = avfilter_link(split_Context, 1, overlay_Context, 0);
    check_result(RESULT, "link scale index 1  to overlay index 0")

    // link overlay 0 to resultsink 0;
    RESULT = avfilter_link(overlay_Context, 0, result_sink_Context, 0);
    check_result(RESULT, "link overlay 0 to resultsink 0")

    // open the filter
    RESULT = avfilter_graph_config(avFilterGraph, NULL);
    check_result(RESULT, "open the filter")
    return RESULT;

}

int save_yuv_to_file(AVFrame *frame, int count) {
    char file_name[200] = {0};
    sprintf(file_name, "%s/yuv420p_%d.yuv", YUV_OUT_URI, count);
    FILE *fp = fopen(file_name, "wb+");
    fwrite(frame->data[0], 1, frame->linesize[0] * frame->height, fp);
    fwrite(frame->data[1], 1, frame->linesize[1] * frame->height / 2, fp);
    fwrite(frame->data[2], 1, frame->linesize[2] * frame->height / 2, fp);
    fclose(fp);
    return RESULT;
}

void free_all() {

}


 int   start_task2() {
    init_ffmpeg();
    start_decoder();
    free_all();
    return RESULT;
}

