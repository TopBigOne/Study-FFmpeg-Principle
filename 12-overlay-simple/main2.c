//
// Created by dev on 2023/4/1.
//

#include <stdio.h>
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libavutil/bprint.h"

#define file_path "/Users/dev/Desktop/mp4/來一碗老于 - 解藥（新版）（原唱：顏小健-鄭國鋒）【動態歌詞】「就在昨天愛悄然離線 等到的只是冷卻的留言」♪.mp4"

#define logo_path "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/12-overlay-simple/doc/logo.jpeg"


AVFormatContext *fmt_ctx      = NULL;
AVCodecContext  *avctx        = NULL;
AVCodec         *codec        = NULL;
AVFilterGraph   *filter_graph = NULL;

AVFilterContext *mainsrc_ctx    = NULL;
AVFilterContext *logo_ctx       = NULL;
AVFilterContext *resultsink_ctx = NULL;

AVPacket   *pkt          = NULL;
AVFrame    *frame        = NULL;
AVFrame    *result_frame = NULL;
AVFrame    *logo_frame   = NULL;
AVRational logo_tb       = {0};
AVRational logo_fr       = {0};

int ret = 0;
int err;


int64_t logo_next_pts = 0;

int read_end  = 0;
int frame_num = 0;

int init_ffmpeg();

int start_decode();

int init_av_filter();

void free_all();

int save_yuv_to_file(AVFrame *frame, int num);

int init_logo_frame();

AVFrame *create_frame_from_jpeg_or_png_file(const char *filename);


int init_ffmpeg() {
    //打开输入文件
    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    if ((err = avformat_open_input(&fmt_ctx, file_path, NULL, NULL)) < 0) {
        fprintf(stderr, "can not open file : %s \n", av_err2str(err));
        return err;
    }

    //打开解码器
    avctx = avcodec_alloc_context3(NULL);
    ret   = avcodec_parameters_to_context(avctx, fmt_ctx->streams[0]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }
    codec    = avcodec_find_decoder(avctx->codec_id);
    if ((ret = avcodec_open2(avctx, codec, NULL)) < 0) {
        printf("open codec faile %d \n", ret);
        return ret;
    }


    pkt          = av_packet_alloc();
    frame        = av_frame_alloc();
    result_frame = av_frame_alloc();
    return ret;


}

int start_decode() {
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
            fprintf(stderr,"outer  AVERROR_EOF\n");
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


        for (;;) {
            //读取 AVFrame
            ret = avcodec_receive_frame(avctx, frame);
            // case 1:
            if (AVERROR(EAGAIN) == ret) {
                fprintf(stderr,"    inner Error EAGAIN\n");
                break;
            }
            // case 2 :
            if (AVERROR_EOF == ret) {
                fprintf(stderr," AVERROR_EOF\n");
                read_end = 1;
                break;
            }
            if (ret >= 0) {
                init_av_filter();
                if (frame_num <= 10) {
                    ret = av_buffersrc_add_frame_flags(mainsrc_ctx, frame, AV_BUFFERSRC_FLAG_PUSH);
                    if (ret < 0) {
                        printf("Error: av_buffersrc_add_frame failed\n");
                        return ret;
                    }
                }
                ret = av_buffersink_get_frame_flags(resultsink_ctx, result_frame, AV_BUFFERSINK_FLAG_NO_REQUEST);
                if (ret == AVERROR_EOF) {
                    //没有更多的 AVFrame
                    printf("no more avframe output \n");
                } else if (ret == AVERROR(EAGAIN)) {
                    //需要输入更多的 AVFrame
                    printf("need more avframe input \n");
                } else if (ret >= 0) {
                    //保存进去文件。
                    printf("save_yuv_to_file success %d \n", frame_num);
                    save_yuv_to_file(result_frame, frame_num);
                }

                logo_next_pts = frame->pts + frame->pkt_duration;
                frame_num++;
                //只保存 10 张图片
                if (frame_num > 10) {
                    ret = av_buffersrc_close(mainsrc_ctx, logo_next_pts, AV_BUFFERSRC_FLAG_PUSH);
                }
                continue;

            }
            printf("other fail \n");
            return ret;

        }


    }
    return ret;

}


int init_av_filter() {
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
        AVRational tb       = fmt_ctx->streams[0]->time_base;
        AVRational fr       = av_guess_frame_rate(fmt_ctx, fmt_ctx->streams[0], NULL);
        AVRational logo_sar = logo_frame->sample_aspect_ratio;
        AVRational sar      = frame->sample_aspect_ratio;
        AVBPrint   args;
        av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
        av_bprintf(&args,
                   "buffer=video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d[main];"
                   "buffer=video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d[logo];"
                   "[main][logo]overlay=x=10:y=10[result];"
                   "[result]format=yuv420p[result_2];"
                   "[result_2]buffersink",
                   frame->width, frame->height, frame->format, tb.num, tb.den, sar.num, sar.den, fr.num,
                   fr.den,
                   logo_frame->width, logo_frame->height, logo_frame->format, logo_tb.num, logo_tb.den,
                   logo_sar.num, logo_sar.den, logo_fr.num, logo_fr.den);


        ret = avfilter_graph_parse2(filter_graph, args.str, &inputs, &outputs);
        if (ret < 0) {
            printf("Cannot configure graph\n");
            return ret;
        }

        //正式打开滤镜
        ret = avfilter_graph_config(filter_graph, NULL);
        if (ret < 0) {
            printf("Cannot configure graph\n");
            return ret;
        }

        //根据 名字 找到 AVFilterContext
        mainsrc_ctx    = avfilter_graph_get_filter(filter_graph, "Parsed_buffer_0");
        logo_ctx       = avfilter_graph_get_filter(filter_graph, "Parsed_buffer_1");
        resultsink_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffersink_4");

        ret = av_buffersrc_add_frame_flags(logo_ctx, logo_frame, AV_BUFFERSRC_FLAG_PUSH);
        if (ret < 0) {
            printf("Error: av_buffersrc_add_frame failed\n");
            return ret;
        }
        //因为 logo 只有一帧，发完，就需要关闭 buffer 滤镜
        logo_next_pts = logo_frame->pts + logo_frame->pkt_duration;
        ret           = av_buffersrc_close(logo_ctx, logo_next_pts, AV_BUFFERSRC_FLAG_PUSH);



    }

    return ret;

}


int main() {
    init_ffmpeg();
    printf("init_ffmpeg result:%d\n", ret);
    init_logo_frame();
    printf("init_logo_frame result:%d\n", ret);
    start_decode();
    printf("start_decode result:%d\n", ret);
    free_all();
    printf("free_all result:%d\n", ret);

    return 0;
}

void free_all() {
    av_frame_free(&frame);
    av_frame_free(&logo_frame);
    av_frame_free(&result_frame);
    av_packet_free(&pkt);

    //关闭编码器，解码器。
    avcodec_close(avctx);

    //关闭输入文件
    avformat_close_input(&fmt_ctx);
    printf("done \n");

    //释放滤镜。
    avfilter_graph_free(&filter_graph);
}


int init_logo_frame() {
    logo_frame = create_frame_from_jpeg_or_png_file(logo_path);
    if (NULL == logo_frame) {
        printf("logo.jpg not exist\n");
        return -1;
    }
    return 0;


}

int save_yuv_to_file(AVFrame *temp_frame, int num) {

    char yuv_file_path[512] = {0};
    sprintf(yuv_file_path,
            "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/12-overlay-simple/doc/yuv/file_%d.yuv",
            num);

    //写入文件
    FILE *fp = NULL;
    fopen(yuv_file_path, "wb+");
    fwrite(temp_frame->data[0], 1, temp_frame->linesize[0] * temp_frame->height, fp);
    fwrite(temp_frame->data[1], 1, temp_frame->linesize[1] * temp_frame->height / 2, fp);
    fwrite(temp_frame->data[2], 1, temp_frame->linesize[2] * temp_frame->height / 2, fp);
    fclose(fp);
    return 0;
}


AVFrame *create_frame_from_jpeg_or_png_file(const char *filename) {

    AVDictionary *format_opts = NULL;
    av_dict_set(&format_opts, "probesize", "5000000", 0);
    AVFormatContext *format_ctx = NULL;
    if ((ret = avformat_open_input(&format_ctx, filename, NULL, &format_opts)) != 0) {
        printf("Error: avformat_open_input failed \n");
        return NULL;
    }
    avformat_find_stream_info(format_ctx, NULL);

    logo_tb = format_ctx->streams[0]->time_base;
    logo_fr = av_guess_frame_rate(format_ctx, format_ctx->streams[0], NULL);

    //打开解码器
    AVCodecContext *av_codec_ctx = avcodec_alloc_context3(NULL);
    ret = avcodec_parameters_to_context(av_codec_ctx, format_ctx->streams[0]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        avformat_close_input(&format_ctx);
        return NULL;
    }

    AVDictionary *codec_opts = NULL;
    av_dict_set(&codec_opts, "sub_text_format", "ass", 0);
    AVCodec *logo_codec = avcodec_find_decoder(av_codec_ctx->codec_id);
    if (!codec) {
        printf("codec not support %d \n", ret);
        return NULL;
    }
    if ((ret = avcodec_open2(av_codec_ctx, logo_codec, NULL)) < 0) {
        printf("open codec faile %d \n", ret);
        return NULL;
    }

    AVPacket *temp_pkt = av_packet_alloc();

    temp_pkt->data = NULL;
    temp_pkt->size = 0;
    ret = av_read_frame(format_ctx, temp_pkt);
    ret = avcodec_send_packet(av_codec_ctx, temp_pkt);
    AVFrame *frame_2 = av_frame_alloc();
    ret = avcodec_receive_frame(av_codec_ctx, frame_2);
    printf("create_frame_from_jpeg_or_png_file %d \n", ret);
    //省略错误处理

    av_dict_free(&format_opts);
    av_dict_free(&codec_opts);

    return frame_2;
}
