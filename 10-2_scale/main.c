#include <stdio.h>
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libavutil/bprint.h"

int ret = 0;
int err = 0;

int read_end  = 0;
int frame_num = 0;

AVFormatContext *fmt_ctx        = NULL;
AVCodecContext  *avCodecContext = NULL;
AVCodec         *avCodec        = NULL;

AVFilterGraph   *filter_graph    = NULL;
AVFilterContext *main_src_ctx    = NULL;
AVFilterContext *result_sink_ctx = NULL;

AVPacket *pkt          = NULL;
AVFrame  *frame        = NULL;
AVFrame  *result_frame = NULL;

// Filter
AVFilterInOut *inputs;
AVFilterInOut *cur;
AVFilterInOut *outputs;


int save_yuv_to_file(AVFrame *frame, int num);

int init_ffmpeg();

int start_decoder();

void init_filter_graph();

void free_all();

int main() {
    ret = init_ffmpeg();
    printf("main ret : %d\n", ret);
    ret = start_decoder();
    printf("main ret : %d\n", ret);
    free_all();
    return 0;
}

int init_ffmpeg() {
    //打开输入文件
    char filename[] = "/Users/dev/Desktop/yuv_data/juren-30s.mp4";
    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    err = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    if (err < 0) {
        printf("can not open file %d \n", err);
        return err;
    }
    avCodecContext = avcodec_alloc_context3(NULL);
    ret            = avcodec_parameters_to_context(avCodecContext, fmt_ctx->streams[0]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }

    avCodec = avcodec_find_decoder(avCodecContext->codec_id);
    if (ret < 0) {
        printf("open codec faile %d \n", ret);
        return ret;
    }
    ret = avcodec_open2(avCodecContext, avCodec, NULL);
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 in ERROR.");
        return ret;
    }


    pkt          = av_packet_alloc();
    frame        = av_frame_alloc();
    result_frame = av_frame_alloc();

    return ret;
}


void init_filter_graph() {
    if (filter_graph != NULL) {
        return;
    }
    filter_graph = avfilter_graph_alloc();

    if (!filter_graph) {
        printf("Error: allocate filter graph failed\n");
        return;
    }

    AVBPrint args;
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    AVRational tb  = fmt_ctx->streams[0]->time_base;
    AVRational fr  = av_guess_frame_rate(fmt_ctx, fmt_ctx->streams[0], NULL);
    AVRational sar = frame->sample_aspect_ratio;
    av_bprintf(&args,
               "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
               frame->width, frame->height, frame->format, tb.num, tb.den, sar.num, sar.den, fr.num, fr.den);
    // 创建buffer滤镜 ctx
    ret = avfilter_graph_create_filter(&main_src_ctx, avfilter_get_by_name("buffer"),
                                       "Parsed_buffer_0_666", args.str, NULL, filter_graph);
    printf("main_src_ctx name : %s\n",main_src_ctx->name);
    if (ret < 0) {
        fprintf(stderr, "buffer ctx fail \n");
        return;
    }
    // 创建buffer sink 滤镜ctx
    ret = avfilter_graph_create_filter(&result_sink_ctx, avfilter_get_by_name("buffersink"),
                                       "Parsed_buffer_2_888", NULL, NULL, filter_graph);
    printf("result_sink_ctx name : %s\n",result_sink_ctx->name);

    if (ret < 0) {
        fprintf(stderr, "buffersink ctx fail\n");
        return;
    }
    // 用 avfilter_graph_parse2 创建scale 滤镜
    av_bprint_clear(&args);
    av_bprintf(&args, "[0:v]scale=%d:%d", frame->width / 2, frame->height / 2);

    // 解析滤镜字符串
    ret = avfilter_graph_parse2(filter_graph, args.str, &inputs, &outputs);
    if (ret < 0) {
        printf("Cannot configure graph\n");
        return;
    }
    for (cur = inputs; cur; cur = cur->next) {
        printf("cur->name : %s, cur->filter_ctx->name : %s \n", cur->name,
               cur->filter_ctx->name);
        // 连接buffer 滤镜跟scale 滤镜
        if ((ret = avfilter_link(main_src_ctx, 0, cur->filter_ctx, 0)) < 0) {
            fprintf(stderr, "link ctx fail: %d\n", ret);
            return;

        }
    }

    for (cur = outputs; cur; cur = cur->next) {
        printf("cur->name : %s, cur->filter_ctx->name : %s \n", cur->name,
               cur->filter_ctx->name);
        if ((ret = avfilter_link(cur->filter_ctx, 0, result_sink_ctx, 0)) < 0) {
            printf("link ctx fail\n");
            return;
        }
    }

    ret = avfilter_graph_config(filter_graph, NULL);
    if (ret < 0) {
        printf("Cannot configure graph\n");
        return;
    }


}

int start_decoder() {
    for (;;) {
        if (read_end == 1) {
            break;
        }
        ret = av_read_frame(fmt_ctx, pkt);
        if (pkt->stream_index == 1) {
            av_packet_unref(pkt);
            continue;
        }
        // case 1:
        if (ret == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);

        } else {
            if (ret != 0) {
                printf("read error code %d \n", ret);
                return ENOMEM;
            }
            TAG_RETRY:
            if (avcodec_send_packet(avCodecContext, pkt) == AVERROR(EAGAIN)) {
                printf("Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                goto TAG_RETRY;
            }

            av_packet_unref(pkt);

        }

        // 循环不断从解码器读数据，直到没有数据可读。
        for (;;) {
            ret = avcodec_receive_frame(avCodecContext, frame);
            // case 1:
            if (ret == AVERROR(EAGAIN)) {
                break;
            }
            // case 2:
            if (ret == AVERROR_EOF) {
                read_end = 1;
                break;
            }
            // case 3:
            if (ret >= 0) {
                init_filter_graph();

                ret = av_buffersrc_add_frame_flags(main_src_ctx, frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret < 0) {
                    fprintf(stderr, "ERROR: av_buffersrc_add_frame failed\n");
                    return ret;
                }

                ret = av_buffersink_get_frame_flags(result_sink_ctx, result_frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret >= 0) {
                    //保存进去文件。
                   //  printf("save_yuv_to_file success\n");
                    save_yuv_to_file(result_frame, frame_num);

                }
                frame_num++;
                if (frame_num > 10) {
                    return 666;
                }
                continue;
            }

            fprintf(stderr, "other fail \n");
            return ret;
        }
    }

    return ret;


}

void free_all() {
    av_frame_free(&frame);
    av_frame_free(&result_frame);
    av_packet_free(&pkt);

    avcodec_close(avCodecContext);
    avformat_free_context(fmt_ctx);

    avfilter_graph_free(&filter_graph);


}

int save_yuv_to_file(AVFrame *tempFrame, int num) {
    //拼接文件名
    char yuv_pic_name[200] = {0};

    sprintf(yuv_pic_name,
            "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-2_scale/doc/yuv420p_%d.yuv", num);

    //写入文件
    FILE *fp = NULL;
    fp = fopen(yuv_pic_name, "wb+");
    fwrite(tempFrame->data[0], 1, tempFrame->linesize[0] * tempFrame->height, fp);
    fwrite(tempFrame->data[1], 1, tempFrame->linesize[1] * tempFrame->height / 2, fp);
    fwrite(tempFrame->data[2], 1, tempFrame->linesize[2] * tempFrame->height / 2, fp);
    fclose(fp);
    return 0;
}
