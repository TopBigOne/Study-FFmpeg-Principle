#include <stdio.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/bprint.h>
#include <libavutil/parseutils.h>

#include <libavutil/samplefmt.h>

int main() {

    int  ret       = 0;
    int  err       = 0;
    char *filename = "/Users/dev/Desktop/mp4/Coldplay - Viva la Vida (Lyrics).mp4";

    AVFormatContext *avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    err = avformat_open_input(&avFormatContext, filename, NULL, NULL);
    if (err < 0) {
        printf("can not open file %d \n", err);
        return err;
    }
    // 打开音频解码器
    AVCodecContext *avCodecContext = avcodec_alloc_context3(NULL);
    ret = avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[1]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }

    AVCodec *avCodec = avcodec_find_decoder(avCodecContext->codec_id);
    ret = avcodec_open2(avCodecContext, avCodec, NULL);
    if (ret < 0) {
        printf("avcodec_open2 code %d \n", ret);
        return ret;
    }

    AVFilterGraph   *avFilterGraph = NULL;
    AVFilterContext *main_src_ctx;
    AVFilterContext *resultsink_ctx;

    AVPacket *pkt          = av_packet_alloc();
    AVFrame  *frame        = av_frame_alloc();
    AVFrame  *result_frame = av_frame_alloc();

    const char *origin_sample_fmt     = NULL;
    const char *origin_sample_rate    = NULL;
    const char *origin_channel_layout = NULL;
    const char *result_sample_fmt     = NULL;
    const char *result_sample_rate    = NULL;
    const char *result_channel_layout = NULL;

    int read_end  = 0;
    int frame_num = 0;
    for (;;) {
        if (read_end == 1) {
            break;
        }
        ret = av_read_frame(avFormatContext, pkt);
        if (pkt->stream_index == 0) {
            av_packet_unref(pkt);
            continue;
        }
        if (ret == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);
        } else {
            if (ret != 0) {
                return ENOMEM;
            }
            TAG_RETRY:
            if (avcodec_send_packet(avCodecContext, pkt) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
        }

        for (;;) {
            ret = avcodec_receive_frame(avCodecContext, frame);
            if (AVERROR(EAGAIN) == ret) {

                break;
            }
            if (AVERROR_EOF == ret) {
                read_end = 1;
                break;
            }
            if (ret >= 0) {
                AVFilterInOut *inputs;
                AVFilterInOut *outputs;
                origin_sample_fmt = av_get_sample_fmt_name(frame->format);
                printf("* origin frame is %d, fmt name is : %s | %d | %d ,pts:%d,nb_samples:%d,duration:%d\n",
                       frame->format,
                       origin_sample_fmt,
                       frame->sample_rate,
                       (int) frame->channel_layout,
                       (int) frame->pts,
                       (int) frame->pkt_duration);
                if (avFilterGraph == NULL) {
                    avFilterGraph = avfilter_graph_alloc();
                    AVRational tb = avFormatContext->streams[0]->time_base;
                    AVBPrint   args;
                    av_bprint_init(&args,0,AV_BPRINT_SIZE_AUTOMATIC);
                    av_bprintf(&args,
                               "abuffer=sample_rate=%d:sample_fmt=%s:channel_layout=%d:time_base=%d/%d[main];"
                               "[main]aformat=sample_rates=%d:sample_fmts=%s:channel_layouts=%d[result];"
                               "[result]abuffersink",
                               frame->sample_rate, origin_sample_fmt, (int) frame->channel_layout, tb.num, tb.den,
                               "44100", "s64",
                               AV_CH_FRONT_RIGHT);


                    // 解析滤镜字符串
                    ret = avfilter_graph_parse2(avFilterGraph, args.str, &inputs, &outputs);
                    if (ret < 0) {
                        fprintf(stderr,"Cannot parse graph\n");
                        return ret;
                    }

                    // 正式打开滤镜
                    ret = avfilter_graph_config(avFilterGraph, NULL);
                    if (ret < 0) {
                        printf("Cannot configure graph\n");
                        return ret;
                    }
                    main_src_ctx   = avfilter_graph_get_filter(avFilterGraph, "Parsed_abuffer_0");
                    resultsink_ctx = avfilter_graph_get_filter(avFilterGraph, "Parsed_abuffersink_2");


                }

                ret = av_buffersrc_add_frame_flags(main_src_ctx, frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret < 0) {
                    printf("Error: av_buffersrc_add_frame failed\n");
                    return ret;
                }

                ret = av_buffersink_get_frame_flags(resultsink_ctx, result_frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret >= 0) {
                    result_sample_fmt = av_get_sample_fmt_name(result_frame->format);
                    printf("~ result frame is %d,fmt name is : %s | %d | %d ,pts:%d, nb_samples:%d , duration:%d \n\n",
                           result_frame->format,
                           result_sample_fmt, result_frame->sample_rate,
                           (int) result_frame->channel_layout,
                           (int) result_frame->pts, result_frame->nb_samples, (int) result_frame->pkt_duration);
                }

                frame_num++;
                if (frame_num > 10) {
                    return 666;
                }

                continue;
            }
            fprintf(stderr, "other filed.\n");

        }

    }

    av_frame_free(&frame);
    av_frame_free(&result_frame);
    av_packet_free(&pkt);

    //关闭编码器，解码器。
    avcodec_close(avCodecContext);

    //释放容器内存。
    avformat_free_context(avFormatContext);
    printf("done \n");

    //释放滤镜。
    avfilter_graph_free(&avFilterGraph);

    return ret;
}
