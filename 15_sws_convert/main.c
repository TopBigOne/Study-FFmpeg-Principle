#include <stdio.h>

#include <libavformat/avformat.h>
#include <libswresample/swresample.h>


int my_code();

int luo_code();

int main() {
    my_code();
    // luo_code();
    return 0;
}

int my_code() {
    int read_end = 0;
    int result   = 0;

    const char *file_name = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/15_sws_convert/data/juren-30s.mp4";

    AVFormatContext *avFormatContext = NULL;
    AVCodecContext  *avCodecContext  = NULL;
    AVCodec         *avCodec         = NULL;

    AVPacket *avPacket = av_packet_alloc();
    AVFrame  *avFrame  = av_frame_alloc();

    avFormatContext = avformat_alloc_context();
    avformat_open_input(&avFormatContext, file_name, NULL, NULL);
    avformat_find_stream_info(avFormatContext, NULL);
    avCodecContext = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[1]->codecpar);

    avCodec = avcodec_find_decoder(avCodecContext->codec_id);
    avcodec_open2(avCodecContext, avCodec, NULL);

    // sws_convert part.
    const char        *format_name = NULL;
    struct SwrContext *swr_ctx     = NULL;
    uint8_t           *out         = NULL;

    int out_count;
    int out_size;
    int out_nb_samples;
    int tag_fmt   = AV_SAMPLE_FMT_S64;
    int tgt_freg  = 44100;
    int tgt_channels;
    int frame_num = 0;


    for (;;) {
        if (read_end == 1) {
            break;
        }
        result = av_read_frame(avFormatContext, avPacket);
        if (avPacket->stream_index == 0) {
            av_packet_unref(avPacket);
            continue;
        }

        if (result == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, avPacket);

        } else if (result == 0) {
            TAG_RETRY:
            if ((result = avcodec_send_packet(avCodecContext, avPacket)) == AVERROR(EAGAIN)) {
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
                //解码出一帧 音频PCM 数据，打印一些信息。
                if (frame_num == 0) {
                    format_name = av_get_sample_fmt_name(avFrame->format);
                    printf("origin : sample_format : %s, sample_rate : %d.\n", format_name,
                           avFrame->sample_rate);
                    format_name = av_get_sample_fmt_name(tag_fmt);
                    printf("target : sample_format : %s, sample_rate : %d.\n", format_name, tgt_freg);
                }

                if (swr_ctx == NULL) {
                    // step 1:
                    swr_ctx = swr_alloc_set_opts(NULL,
                                                 (int64_t) avFrame->channel_layout,
                                                 tag_fmt,
                                                 tgt_freg,
                                                 (int64_t) avFrame->channel_layout,
                                                 avFrame->format,
                                                 avFrame->sample_rate,
                                                 0,
                                                 NULL);
                    if (swr_ctx == NULL) {
                        return -1;
                    }
                    result = swr_init(swr_ctx);
                    if (result < 0) {
                        av_log(NULL, AV_LOG_ERROR, "can`t create sample rate converter.\n");
                        swr_free(&swr_ctx);
                        return result;
                    }

                }


                // 不改变声道布局
                tgt_channels = avFrame->channels;
                // 由于源文件 juren-30s.mp4 大部分音频帧是 1024 个样本数，从 48000 降低 44100，也就是说 1024个样本 会 变成 940 个样本;
                // 所以 out_count 通常会在本来的大小上 加上 256，让写空间大一点。
                out_count    = (int64_t) avFrame->nb_samples * tgt_freg / avFrame->sample_rate + 256;
                out_size     = av_samples_get_buffer_size(NULL, tgt_channels, out_count, tag_fmt, 0);

                out = av_malloc(out_size);
                // 注意，因为 音频可能 有超过9个声道的数据，所以要用 extended_data;
                const uint8_t **in = (const uint8_t **) avFrame->extended_data;

                out_nb_samples = swr_convert(swr_ctx, &out, out_count, in, avFrame->nb_samples);
                if (out_nb_samples < 0) {
                    av_log(NULL, AV_LOG_ERROR, "converter fail\n");
                } else {
                    printf("out_count: %d,out_nb_samples:%d,avFrame->nb_samples:%d\n",
                           out_count, out_nb_samples, avFrame->nb_samples);
                }
                // tips:可以把out的内存直接丢给 播放器播放
                av_freep(&out);

                frame_num++;
                if (frame_num > 10) {
                    out = av_malloc(out_size);
                    do {
                        // 把剩下的样本数 都刷出来
                        out_nb_samples = swr_convert(swr_ctx, &out, out_count, NULL, 0);
                        printf("flush out_count : %d,out_nb_samples: %d \n", out_count, out_nb_samples);
                    } while (out_nb_samples);
                    av_freep(&out);
                    return 99;
                }
                continue;
            }
            fprintf(stderr, "other ERROR.\n");
        }


    }

    av_frame_free(&avFrame);
    av_packet_free(&avPacket);

    //关闭解码器。
    avcodec_close(avCodecContext);
    return result;
}

int luo_code() {

    AVFormatContext *fmt_ctx  = NULL;
    int             type      = 1;
    int             ret       = 0;
    int             frame_num = 0;
    int             read_end  = 0;
    AVPacket        *pkt      = av_packet_alloc();
    AVFrame         *frame    = av_frame_alloc();


    int        err;
    //提示，要把 juren-30s.mp4 文件放到 Debug 目录下才能找到。
    const char *file_name = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/15_sws_convert/data/juren-30s.mp4";
    // char filename[] = "juren-30s.mp4";

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }

    if ((err = avformat_open_input(&fmt_ctx, file_name, NULL, NULL)) < 0) {
        printf("can not open file %d \n", err);
        return err;
    }

    //本文只初始化 音频解码器。视频解码器不管
    AVCodecContext *avctx = avcodec_alloc_context3(NULL);
    //把容器记录的编码参数赋值给 编码器上下文。
    ret = avcodec_parameters_to_context(avctx, fmt_ctx->streams[1]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }

    AVCodec *codec = avcodec_find_decoder(avctx->codec_id);
    if ((ret = avcodec_open2(avctx, codec, NULL)) < 0) {
        printf("open codec faile %d \n", ret);
        return ret;
    }

    const char        *format_name = NULL;
    struct SwrContext *swr_ctx     = NULL;
    uint8_t           *out         = NULL;
    int               out_count;
    int               out_size;
    int               out_nb_samples;
    int               tgt_fmt      = AV_SAMPLE_FMT_S64;
    int               tgt_freq     = 44100;
    int               tgt_channels;

    for (;;) {
        if (1 == read_end) {
            break;
        }

        ret = av_read_frame(fmt_ctx, pkt);
        //跳过不处理音频包
        if (0 == pkt->stream_index) {
            av_packet_unref(pkt);
            continue;
        }
        if (AVERROR_EOF == ret) {
            //读取完文件，这时候 pkt 的 data 跟 size 应该是 null
            avcodec_send_packet(avctx, pkt);
        } else if (0 == ret) {
            retry:
            if (avcodec_send_packet(avctx, pkt) == AVERROR(EAGAIN)) {
                printf("Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                //这里可以考虑休眠 0.1 秒，返回 EAGAIN 通常是 ffmpeg 的内部 api 有bug
                goto retry;
            } else {
                //释放 pkt 里面的内存数据
                av_packet_unref(pkt);

                //循环不断从解码器读数据，直到没有数据可读。
                for (;;) {
                    //读取 AVFrame
                    ret = avcodec_receive_frame(avctx, frame);
                    /* 释放 frame 里面的YUV数据，
                     * 由于 avcodec_receive_frame 函数里面会调用 av_frame_unref，所以下面的代码可以注释。
                     * 所以我们不需要 手动 unref 这个 AVFrame，当然你 unref 多一次也不会出错。
                     * */
                    //av_frame_unref(frame);

                    if (AVERROR(EAGAIN) == ret) {
                        //提示 EAGAIN 代表 解码器 需要 更多的 AVPacket
                        //跳出 第一层 for，让 解码器拿到更多的 AVPacket
                        break;
                    } else if (AVERROR_EOF == ret) {
                        /* 提示 AVERROR_EOF 代表之前已经往 解码器发送了一个 data 跟 size 都是 NULL 的 AVPacket
                         * 发送 NULL 的 AVPacket 是提示解码器把所有的缓存帧全都刷出来。
                         * 通常只有在 读完输入文件才会发送 NULL 的 AVPacket，或者需要用现有的解码器解码另一个的视频流才会这么干。
                         * */
                        //跳出 第二层 for，文件已经解码完毕。
                        read_end = 1;
                        break;
                    } else if (ret >= 0) {
                        //解码出一帧 音频PCM 数据，打印一些信息。
                        if (frame_num == 0) {
                            format_name = av_get_sample_fmt_name(frame->format);
                            printf("origin sample_format:%s, sample_rate:%d .\n", format_name, frame->sample_rate);
                            format_name = av_get_sample_fmt_name(tgt_fmt);
                            printf("target sample_format:%s, sample_rate:%d .\n", format_name, tgt_freq);
                        }

                        if (!swr_ctx) {
                            swr_ctx = swr_alloc_set_opts(NULL,
                                                         frame->channel_layout, tgt_fmt, tgt_freq,
                                                         frame->channel_layout, frame->format, frame->sample_rate,
                                                         0, NULL);
                            if (!swr_ctx || swr_init(swr_ctx) < 0) {
                                av_log(NULL, AV_LOG_ERROR, "Cannot create sample rate converter \n");
                                swr_free(&swr_ctx);
                                return -1;
                            }
                        }

                        //不改变声道布局
                        tgt_channels = frame->channels;

                        out_count    = (int64_t) frame->nb_samples * tgt_freq /*44100*// frame->sample_rate + 256;
                        out_size     = av_samples_get_buffer_size(NULL, tgt_channels, out_count, tgt_fmt, 0);
                        out          = av_malloc(out_size);
                        //注意，因为 音频可能有超过 9 声道的数据，所以要用 extended_data;
                        const uint8_t **in = (const uint8_t **) frame->extended_data;

                        out_nb_samples = swr_convert(swr_ctx, &out, out_count, in, frame->nb_samples);
                        if (out_nb_samples < 0) {
                            av_log(NULL, AV_LOG_ERROR, "converte fail \n");
                        } else {
                            printf("out_count:%d, "
                                   " out_nb_samples:%d, "
                                   " frame->nb_samples:%d  \n", out_count,
                                   out_nb_samples, frame->nb_samples);
                        }

                        //可以把 out 的内存直接丢给播放器播放。
                        av_freep(&out);

                        frame_num++;
                        if (frame_num > 10) {
                            out = av_malloc(out_size);
                            do {
                                //把剩下的样本数都刷出来。
                                out_nb_samples = swr_convert(swr_ctx, &out, out_count, NULL, 0);
                                printf("flush out_count:%d, out_nb_samples:%d  \n", out_count, out_nb_samples);
                            } while (out_nb_samples);
                            av_freep(&out);

                            return 99;
                        }
                    } else {
                        printf("other fail \n");
                        return ret;
                    }
                }
            }

        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);

    //关闭解码器。
    avcodec_close(avctx);

    return 0;
}

