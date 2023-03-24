#include <stdio.h>

#include <libavformat/avformat.h>

#define VIDEO_PATH "/Users/dev/Desktop/mp4/全是爱.mp4"

int test_my();

int test_luo();

int test_my() {
    int ret = 0;
    int err = 0;

    AVFormatContext *avFormatContext = avformat_alloc_context();
    err = avformat_open_input(&avFormatContext, VIDEO_PATH, NULL, NULL);

    AVCodecContext *decodeContext = avcodec_alloc_context3(NULL);
    // paramters to Decodec Context
    ret = avcodec_parameters_to_context(decodeContext, avFormatContext->streams[0]->codecpar);
    if (ret < 0) {
        return ret;
    }
    AVCodec *decoderCodec = avcodec_find_decoder(decodeContext->codec_id);
    // 打开解码器
    ret = avcodec_open2(decodeContext, decoderCodec, NULL);
    if (ret < 0) {
        printf("open codec failed %d \n", ret);
        return ret;
    }
    // -------------------------------------------------------------------------------->
    char            file_name_out[] = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/7_avio_open2/doc/juren-30s-5.mp4";
    AVFormatContext *fmt_ctx_out    = NULL;
    // 申请 输出上下文
    err = avformat_alloc_output_context2(&fmt_ctx_out, NULL, NULL, file_name_out);
    if (!fmt_ctx_out) {
        printf("avformat_alloc_output_context2 : error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    // 添加一路流到容器上下文
    AVStream *st = avformat_new_stream(fmt_ctx_out, NULL);
    // 设置 time base;
    st->time_base = avFormatContext->streams[0]->time_base;

    // 编码器上下文
    AVCodecContext *encodeContext = NULL;
    AVPacket       *avPacket      = av_packet_alloc();
    AVFrame        *avFrame       = av_frame_alloc();
    AVPacket       *pkt_out       = av_packet_alloc();
    int            frame_out      = 0;
    int            read_end       = 0;

    for (;;) {
        if (read_end == 1) {
            break;
        }


        ret = av_read_frame(avFormatContext, avPacket);
        if (avPacket->stream_index == 1) {
            av_packet_unref(avPacket);
            continue;
        }

        if (ret == AVERROR_EOF) {
            avcodec_send_packet(decodeContext, NULL);
            continue;
        }
        if (ret != 0) {
            return ENOMEM;
        }

        tag_retry:
        if (avcodec_send_packet(decodeContext, avPacket) == AVERROR(EAGAIN)) {
            goto tag_retry;
        }
        // 释放 avPacket 中的数据
        av_packet_unref(avPacket);

        // 循环 从解码器中获取 frame数据
        for (;;) {
            ret = avcodec_receive_frame(decodeContext, avFrame);
            // case 1:
            if (ret == AVERROR(EAGAIN)) {
                break;
            }

            // case 2: AVERROR_EOF: 可以读取数据了
            if (ret == AVERROR_EOF) {
                // 往编码器中发送一个 NULL的 AVFrame，让编码器，把剩下的数据刷刷出来
                ret = avcodec_send_frame(encodeContext, NULL);

                for (;;) {
                    ret = avcodec_receive_packet(encodeContext, pkt_out);
                    // case 1:
                    if (ret == AVERROR(EAGAIN)) {
                        return ret;
                    }
                    // case 2:
                    if (ret == AVERROR_EOF) {
                        break;
                    }
                    // case 3: 编码出AVPacket ，先打印一些信息，然后，把它 写入文件
                    if (ret >= 0) {
                        printf(" case 3: 编码出AVPacket : pkt_out size : %d \n", pkt_out->size);
                        // 设置AVPacket 的stream_index ，这样，才知道是哪一个流的；
                        pkt_out->stream_index = st->index;
                        // 转换： AVPacket 的时间基 为输出流的时间基
                        pkt_out->pts          = av_rescale_q_rnd(pkt_out->pts, fmt_ctx_out->streams[0]->time_base,
                                                                 st->time_base,
                                                                 AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

                        pkt_out->dts = av_rescale_q_rnd(pkt_out->dts, fmt_ctx_out->streams[0]->time_base,
                                                        st->time_base,
                                                        AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

                        pkt_out->duration = av_rescale_q_rnd(pkt_out->duration, fmt_ctx_out->streams[0]->time_base,
                                                             st->time_base,
                                                             AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

                        ret = av_interleaved_write_frame(fmt_ctx_out, pkt_out);
                        if (ret < 0) {
                            fprintf(stderr, "av_interleaved_write_frame failed, %d\n", ret);
                            return ret;
                        } else{
                            fprintf(stdout, "av_interleaved_write_frame ok.");
                        }
                        av_packet_unref(pkt_out);
                    }

                }
                av_write_trailer(fmt_ctx_out);
                //跳出 第二层 for，文件已经解码完毕。
                read_end = 1;
                break;
            }

            // case 3: ret >=0;
            if (ret >= 0) {
                // encodeContext 为NULL，需要先做一些初始化工作；
                if (encodeContext == NULL) {
                    AVCodec *encodeCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
                    encodeContext = avcodec_alloc_context3(encodeCodec);
                    encodeContext->codec_type             = AVMEDIA_TYPE_VIDEO;
                    encodeContext->bit_rate               = 400000;
                    encodeContext->framerate              = decodeContext->framerate;
                    encodeContext->gop_size               = 30;
                    encodeContext->max_b_frames           = 10;
                    encodeContext->profile                = FF_PROFILE_H264_MAIN;
                    encodeContext->time_base              = avFormatContext->streams[0]->time_base;
                    encodeContext->height                 = avFormatContext->streams[0]->codecpar->height;
                    encodeContext->width                  = avFormatContext->streams[0]->codecpar->width;
                    encodeContext->sample_aspect_ratio    = st->sample_aspect_ratio = avFrame->sample_aspect_ratio;
                    encodeContext->pix_fmt                = avFrame->format;
                    encodeContext->color_range            = avFrame->color_range;
                    encodeContext->color_primaries        = avFrame->color_primaries;
                    encodeContext->color_trc              = avFrame->color_trc;
                    encodeContext->colorspace             = avFrame->colorspace;
                    encodeContext->chroma_sample_location = avFrame->chroma_location;
                    encodeContext->field_order            = AV_FIELD_PROGRESSIVE;

                    ret = avcodec_parameters_from_context(st->codecpar, encodeContext);
                    if (ret < 0) {
                        return ret;
                    }
                    if ((ret = avcodec_open2(encodeContext, encodeCodec, NULL)) < 0) {
                        return ret;
                    }
                    //  正式打开输出文件
                    if ((ret = avio_open2(&fmt_ctx_out->pb, file_name_out, AVIO_FLAG_WRITE,
                                          &fmt_ctx_out->interrupt_callback, NULL)) < 0) {
                        printf("avio_open2 fail %d \n", ret);
                        return ret;
                    }
                    // 要先写入 文件头部
                    ret      = avformat_write_header(fmt_ctx_out, NULL);
                    if (ret < 0) {
                        printf("avformat_write_header fail %d \n", ret);
                        return ret;

                    }

                }

                // 往编码器 发送AVFrame ，然后不断地读取AVPacket
                ret = avcodec_send_frame(encodeContext, avFrame);
                if (ret < 0) {
                    return ret;
                }
                for (;;) {
                    ret = avcodec_receive_packet(encodeContext, pkt_out);
                    // case 1:
                    if (ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    // case 2:
                    if (ret < 0) {
                        printf("avcodec_receive_packet fail %d \n", ret);
                        return ret;
                    }
                    // case 3:
                    if (ret >= 0) {
                        printf("pkt_out size : %d \n", pkt_out->size);
                        pkt_out->stream_index = st->index;

                        // 转换AVPacket 的时间基 为输出流的时间基
                        pkt_out->pts      = av_rescale_q_rnd(pkt_out->pts, fmt_ctx_out->streams[0]->time_base,
                                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        pkt_out->dts      = av_rescale_q_rnd(pkt_out->dts, fmt_ctx_out->streams[0]->time_base,
                                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        pkt_out->duration = av_rescale_q_rnd(pkt_out->duration, fmt_ctx_out->streams[0]->time_base,
                                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

                        ret = av_interleaved_write_frame(fmt_ctx_out, pkt_out);

                        if (ret < 0) {
                            printf("av_interleaved_write_frame faile %d \n", ret);
                            return ret;
                        } else{
                            fprintf(stdout,"av_interleaved_write_frame OK;  %d \n", ret);
                        }
                        av_packet_unref(pkt_out);

                    }
                }
                continue;
            }
            // case 4:
            printf("other fail \n");
            return ret;

        }


    }

    av_frame_free(&avFrame);
    av_packet_free(&avPacket);
    av_packet_free(&pkt_out);

    // close the codec
    avcodec_close(decodeContext);
    avcodec_close(encodeContext);

    // 释放容器内存
    avformat_free_context(avFormatContext);
    // 必须调用avio_closep  ，要不可能会把 数据写进去,会是0kb；
    avio_closep(&fmt_ctx_out->pb);
    avformat_free_context(fmt_ctx_out);
    printf("DONE\n");
    return 0;
}

int main() {
    test_my();
   // test_luo();
    return 0;
}



