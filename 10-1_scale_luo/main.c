#include <stdio.h>

#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libavutil/bprint.h"

int save_yuv_to_file(AVFrame *frame, int num);

int main() {

    int ret = 0;
    int err;

    //打开输入文件
    char filename[] = "/Users/dev/Desktop/yuv_data/juren-30s.mp4";

    AVFormatContext *fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    if ((err = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        printf("avformat_open_input # can not open file %d \n", err);
        return err;
    }

    //打开解码器
    AVCodecContext *avctx = avcodec_alloc_context3(NULL);
    ret = avcodec_parameters_to_context(avctx, fmt_ctx->streams[0]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }
    AVCodec *codec = avcodec_find_decoder(avctx->codec_id);
    if ((ret = avcodec_open2(avctx, codec, NULL)) < 0) {
        printf("open codec faile %d \n", ret);
        return ret;
    }

    AVFilterGraph   *filter_graph                             = NULL;
    AVFilterContext *mainsrc_ctx, *scale_ctx, *resultsink_ctx = NULL;


    AVPacket *pkt          = av_packet_alloc();
    AVFrame  *frame        = av_frame_alloc();
    AVFrame  *result_frame = av_frame_alloc();

    int read_end  = 0;
    int frame_num = 0;


    for (;;) {
        if (1 == read_end) {
            break;
        }

        ret = av_read_frame(fmt_ctx, pkt);
        //跳过不处理音频包
        if (1 == pkt->stream_index) {
            av_packet_unref(pkt);
            continue;
        }

        if (AVERROR_EOF == ret) {
            //读取完文件，这时候 pkt 的 data 跟 size 应该是 null
            avcodec_send_packet(avctx, NULL);
        } else {
            if (0 != ret) {
                printf("read error code %d \n", ret);
                return ENOMEM;
            } else {
                retry:
                if (avcodec_send_packet(avctx, pkt) == AVERROR(EAGAIN)) {
                    printf("Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                    //这里可以考虑休眠 0.1 秒，返回 EAGAIN 通常是 ffmpeg 的内部 api 有bug
                    goto retry;
                }
                //释放 pkt 里面的编码数据
                av_packet_unref(pkt);
            }

        }

        //循环不断从解码器读数据，直到没有数据可读。
        for (;;) {
            //读取 AVFrame
            ret = avcodec_receive_frame(avctx, frame);
            if (AVERROR(EAGAIN) == ret) {
                //提示 EAGAIN 代表 解码器 需要 更多的 AVPacket
                //跳出 第一层 for，让 解码器拿到更多的 AVPacket
                break;
            }
            if (AVERROR_EOF == ret) {

                read_end = 1;
                break;
            }
            if (ret >= 0) {
                //这两个变量在本文里没有用的，只是要传进去。
                AVFilterInOut *inputs, *outputs;

                if (NULL == filter_graph) {
                    //初始化滤镜容器
                    filter_graph = avfilter_graph_alloc();
                    if (!filter_graph) {
                        printf("Error: allocate filter graph failed\n");
                        return -1;
                    }

                    // 因为 filter 的输入是 AVFrame ，所以 filter 的时间基就是 AVFrame 的时间基
                    AVRational tb  = fmt_ctx->streams[0]->time_base;
                    AVRational fr  = av_guess_frame_rate(fmt_ctx, fmt_ctx->streams[0], NULL);
                    AVRational sar = frame->sample_aspect_ratio;
                    AVBPrint   args;
                    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
                    av_bprintf(&args,
                               "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
                               frame->width, frame->height, frame->format, tb.num, tb.den, sar.num, sar.den, fr.num,
                               fr.den);
                    //创建 buffer 滤镜 ctx
                    ret = avfilter_graph_create_filter(&mainsrc_ctx, avfilter_get_by_name("buffer"),
                                                       "Parsed_buffer_0_666", args.str, NULL, filter_graph);
                    if (ret < 0) {
                        printf("buffer ctx fail \n");
                        return ret;
                    }

                    //创建 scale 滤镜 ctx
                    av_bprint_clear(&args);
                    av_bprintf(&args, "%d:%d", frame->width / 2, frame->height / 2);
                    ret = avfilter_graph_create_filter(&scale_ctx,
                                                       avfilter_get_by_name("scale"),
                                                       "Parsed_scale_1_777", args.str, NULL, filter_graph);
                    if (ret < 0) {
                        printf("scale ctx fail \n");
                        return ret;
                    }

                    //连接 buffer滤镜 跟 scale 滤镜
                    if ((ret = avfilter_link(mainsrc_ctx, 0, scale_ctx, 0)) < 0) {
                        printf("link ctx fail\n");
                        return ret;
                    }

                    //创建 buffersink 滤镜 ctx
                    ret = avfilter_graph_create_filter(&resultsink_ctx, avfilter_get_by_name("buffersink"),
                                                       "Parsed_buffer_2_888", NULL, NULL, filter_graph);
                    if (ret < 0) {
                        printf("buffersink ctx fail\n");
                        return ret;
                    }

                    //连接 scale滤镜 跟 buffersink滤镜
                    if ((ret = avfilter_link(scale_ctx, 0, resultsink_ctx, 0)) < 0) {
                        printf("link ctx fail\n");
                        return ret;
                    }

                    //正式打开滤镜
                    ret = avfilter_graph_config(filter_graph, NULL);
                    if (ret < 0) {
                        printf("Cannot configure graph\n");
                        return ret;
                    }

                }

                ret = av_buffersrc_add_frame_flags(mainsrc_ctx, frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret < 0) {
                    printf("Error: av_buffersrc_add_frame failed\n");
                    return ret;
                }

                ret = av_buffersink_get_frame_flags(resultsink_ctx, result_frame, AV_BUFFERSRC_FLAG_PUSH);
                if (ret >= 0) {
                    //保存进去文件。
                    //  printf("save_yuv_to_file success\n");
                    save_yuv_to_file(result_frame, frame_num);

                }

                frame_num++;
                //只保存 10 张图片
                if (frame_num > 10) {
                    return 666;
                }

                continue;

            }

            printf("other fail \n");
            return ret;

        }


    }

    av_frame_free(&frame);
    av_frame_free(&result_frame);
    av_packet_free(&pkt);

    //关闭编码器，解码器。
    avcodec_close(avctx);

    //释放容器内存。
    avformat_free_context(fmt_ctx);
    printf("done \n");

    //释放滤镜。
    avfilter_graph_free(&filter_graph);

    return 0;
}

int save_yuv_to_file(AVFrame *frame, int num) {

    //拼接文件名
    char yuv_pic_name[200] = {0};
    sprintf(yuv_pic_name,
            "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-1_scale_luo/doc/yuv420p_%d.yuv", num);

    printf("yuv_pic_name : %s\n", yuv_pic_name);

    //写入文件
    FILE *fp = NULL;
    fp = fopen(yuv_pic_name, "wb+");
    fwrite(frame->data[0], 1, frame->linesize[0] * frame->height, fp);
    fwrite(frame->data[1], 1, frame->linesize[1] * frame->height / 2, fp);
    fwrite(frame->data[2], 1, frame->linesize[2] * frame->height / 2, fp);
    fclose(fp);
    return 0;
}