#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/bprint.h>

#define  file_name "/Users/dev/Desktop/yuv_data/juren-30s.mp4"

int save_yuv_to_file(AVFrame *frame, int num);


int ret = 0;
int err = 0;


AVFormatContext *fmt_ctx        = NULL;
AVCodecContext  *avCodecContext = NULL;
AVCodec         *avCodec        = NULL;

AVFilterGraph   *filter_graph    = NULL;
AVFilterContext *main_src_ctx    = NULL;
AVFilterContext *scale_ctx       = NULL;
AVFilterContext *result_sink_ctx = NULL;

AVPacket *pkt          = NULL;
AVFrame  *frame        = NULL;
AVFrame  *result_frame = NULL;

int read_end  = 0;
int frame_num = 0;

int initFFmpeg();

int initFFmpeg() {
    // step 1:
    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    // step 2:
    if ((err       = avformat_open_input(&fmt_ctx, file_name, NULL, NULL)) < 0) {
        return err;
    }
    // step 3: 打开解码器
    avCodecContext = avcodec_alloc_context3(NULL);
    // step 3-1:
    ret            = avcodec_parameters_to_context(avCodecContext, fmt_ctx->streams[0]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }
    // step 3-2:
    avCodec = avcodec_find_decoder(avCodecContext->codec_id);

    // step 3-3;
    ret = avcodec_open2(avCodecContext, avCodec, NULL);
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 in ERROR.");
        return ret;
    }

    pkt          = av_packet_alloc();
    frame        = av_frame_alloc();
    result_frame = av_frame_alloc();

    return 0;
}

int save_yuv_to_file(AVFrame *frame, int num) {
    char yuv_pic_name[200] = {0};
    sprintf(yuv_pic_name,
            "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-1_scale/doc/yuv420p_%d.yuv", num);
    // 写入文件
    FILE *fp = NULL;
    fp = fopen(yuv_pic_name, "wb+");

    fwrite(frame->data[0], 1, frame->linesize[0] * frame->height, fp);
    fwrite(frame->data[1], 1, frame->linesize[1] * frame->height / 2, fp);
    fwrite(frame->data[2], 1, frame->linesize[2] * frame_num / 2, fp);
    return 0;

}

int StartDecoder();


int InitFilterGraph();

int isAudioIndex(AVPacket *packet) {
    if (pkt->stream_index == 1) {
        return 1;
    }
    return 0;
}

int StartDecoder() {
    for (;;) {
        if (read_end == 1) {
            break;
        }
        // 获取下一个packet
        ret = av_read_frame(fmt_ctx, pkt);

        if (isAudioIndex(pkt)) {
            continue;
        }

        // send a NULL AVPacket
        if (ret == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);
            continue;
        }
        if (ret != 0) {
            printf("read error code %d \n", ret);
            return ENOMEM;
        }

        TAG_RETRY:
        if (avcodec_send_packet(avCodecContext, pkt) == AVERROR(EAGAIN)) {
            goto TAG_RETRY;
        }

        av_packet_unref(pkt);

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
                // 初始化滤镜
                InitFilterGraph();

                ret = av_buffersrc_add_frame_flags(main_src_ctx, frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret < 0) {
                    printf("Error: av_buffersrc_add_frame failed\n");
                    return ret;
                }
                ret = av_buffersink_get_frame_flags(result_sink_ctx, result_frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret >= 0) {
                    // 保存进去文件
                    printf("save_yuv_to_file success\n");
                    save_yuv_to_file(result_frame, frame_num);
                }
                frame_num++;
                if (frame_num > 10) {
                    return 666;
                }
                continue;
            }

            printf("other fail \n");
            return ret;

        }
    }

    return ret;


}

int InitFilterGraph() {
    if (filter_graph != NULL) {
        return 0;
    }

    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        printf("InitFilterGraph# Error: allocate filter graph failed\n");
        return -1;
    }
    // 因为filter的输入是AVFrame，所以filter 的时间基就是AVFrame 的时间基
    AVRational tb  = fmt_ctx->streams[0]->time_base;
    AVRational fr  = av_guess_frame_rate(fmt_ctx, fmt_ctx->streams[0], NULL);
    AVRational sar = frame->sample_aspect_ratio;

    AVBPrint args;
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args,
               "video_size = %dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
               frame->width, frame->height, frame->format, tb.num, tb.den, sar.num, sar.den, fr.num, fr.den);
    // AVFilter *bufferFilter = avfilter_get_by_name("buffer");
    // 创建 buffer 滤镜ctx
    ret = avfilter_graph_create_filter(&main_src_ctx, avfilter_get_by_name("buffer"),
                                       "Parsed_buffer_0_666", args.str, NULL, filter_graph);
    if (ret < 0) {
        printf("buffer ctx fail \n");
        return ret;
    }

    // 创建scale 滤镜ctx
    av_bprint_clear(&args);
    av_bprintf(&args, "%d:%d", frame->width / 2, frame->height / 2);

    //  AVFilter *scaleFilter = avfilter_get_by_name("scale");
    ret = avfilter_graph_create_filter(&scale_ctx, avfilter_get_by_name("scale"), "Parsed_scale_1_777", args.str, NULL,
                                       filter_graph);
    if (ret < 0) {
        printf("scale ctx fail \n");
        return ret;
    }

    // 连接 buffer 滤镜跟 scale 滤镜
    if ((ret = avfilter_link(main_src_ctx, 0, scale_ctx, 0)) < 0) {
        printf("link ctx fail\n");
        return ret;
    }

    // 创建buffer sink 滤镜 ctx
    ret = avfilter_graph_create_filter(&result_sink_ctx, avfilter_get_by_name("buffersink"), "Parsed_buffer_2_888",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        printf("buffersink ctx fail\n");
        return ret;
    }

    // 连接scale 滤镜跟buffer sink 滤镜
    if ((ret = avfilter_link(scale_ctx, 0, result_sink_ctx, 0)) < 0) {
        printf("link ctx fail\n");
        return ret;
    }

    // 正式打开滤镜
    ret = avfilter_graph_config(filter_graph, NULL);
    if (ret < 0) {
        printf("Cannot configure graph\n");
        return ret;
    }

    return ret;

}

int main() {

    // step 1: init
    if (initFFmpeg() < 0) {
        return -1;
    }

    // step 2:
    StartDecoder();

    // step 3:
    av_frame_free(&frame);
    av_frame_free(&result_frame);
    av_packet_free(&pkt);

    // 关闭编解码
    avcodec_close(avCodecContext);

    avformat_free_context(fmt_ctx);
    printf("Done\n");
    avfilter_graph_free(&filter_graph);

    return 0;
}
