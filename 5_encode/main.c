#include <stdio.h>

#include <libavformat/avformat.h>
#include <stdbool.h>


#define  VIDEO_PATH "/Users/dev/Desktop/mp4/貳佰《玫瑰》1080P.mp4"


bool isAudioIndex(AVPacket *pPacket);

int main() {
    int ret = 0;
    int err = 0;

    AVFormatContext *avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        return ENOMEM;
    }
    if ((err = avformat_open_input(&avFormatContext, VIDEO_PATH, NULL, NULL)) < 0) {
        return err;
    }

    AVCodecContext *decodeContext = avcodec_alloc_context3(NULL);
    ret = avcodec_parameters_to_context(decodeContext, avFormatContext->streams[0]->codecpar);
    if (ret < 0) {
        return ret;
    }
    AVCodec *decodeCodec = avcodec_find_decoder(decodeContext->codec_id);
    if ((ret = avcodec_open2(decodeContext, decodeCodec, NULL)) < 0) {
        printf("open codec failed %d \n", ret);
        return ret;
    }

    // 编码器编码器上下文
    AVCodecContext *encodeContext = NULL;

    AVPacket *pkt     = av_packet_alloc();
    AVFrame  *frame   = av_frame_alloc();
    AVPacket *pkt_out = av_packet_alloc();

    int read_end = 0;

    // for 1:
    for (;;) {
        if (read_end == 1) {
            break;
        }

        // av_read_frame(); 先读取一个Packet
        ret = av_read_frame(avFormatContext, pkt);
        if (isAudioIndex(pkt)) {
            continue;
        }
        if (ret == AVERROR_EOF) {
            avcodec_send_packet(decodeContext, NULL);

        } else {
            if (ret != 0) {
                printf("read error code %d \n", ret);
                return ENOMEM;
            } else {
                tag_try:
                if (avcodec_send_packet(decodeContext, pkt) == AVERROR(EAGAIN)) {
                    goto tag_try;
                }
                // 解除引用，释放pkt里的数据
                av_packet_unref(pkt);
            }
        }


        // 从packet中取frame;
        // 从解码器中读取数据，直到没有数据可读
        for (;;) {// for 2
            ret = avcodec_receive_frame(decodeContext, frame);
            // 对ret 有3种处理情况:
            // case 1:
            if (ret == AVERROR(EAGAIN)) {
                break;
            } else if (ret == AVERROR_EOF) {// case 2:
                // 针对编码器
                ret = avcodec_send_frame(encodeContext, NULL);
                for (;;) {
                    ret = avcodec_receive_packet(encodeContext, pkt_out);
                    // case 2-1:
                    if (ret == AVERROR(EAGAIN)) {
                        printf("avcodec_receive_packet error code %d \n", ret);
                        return ret;
                    }
                    // case 2-2:
                    if (ret == AVERROR_EOF) {
                        break;
                    }
                    // case 2-3:
                    printf("pkt_out size : %d\n", pkt_out->size);
                }

                read_end = 1;
                break;
            } else if (ret >= 0) { // case 3:
                if (encodeContext == NULL) {
                    AVCodec *encodec = avcodec_find_encoder(AV_CODEC_ID_H264);
                    encodeContext = avcodec_alloc_context3(encodec);
                    encodeContext->codec_type   = AVMEDIA_TYPE_VIDEO;
                    encodeContext->bit_rate     = 400000;
                    encodeContext->framerate    = decodeContext->framerate;
                    encodeContext->gop_size     = 10;
                    encodeContext->max_b_frames = 5;
                    encodeContext->profile      = FF_PROFILE_H264_MAIN;

                    encodeContext->time_base              = avFormatContext->streams[0]->time_base;
                    encodeContext->width                  = avFormatContext->streams[0]->codecpar->width;
                    encodeContext->height                 = avFormatContext->streams[0]->codecpar->height;
                    encodeContext->sample_aspect_ratio    = frame->sample_aspect_ratio;
                    encodeContext->pix_fmt                = frame->format;
                    encodeContext->color_range            = frame->color_range;
                    encodeContext->color_primaries        = frame->color_primaries;
                    encodeContext->color_trc              = frame->color_trc;
                    encodeContext->colorspace             = frame->colorspace;
                    encodeContext->chroma_sample_location = frame->chroma_location;
                    encodeContext->field_order            = AV_FIELD_PROGRESSIVE;
                    if ((ret = avcodec_open2(encodeContext, encodec, NULL)) < 0) {
                        printf("open encode codec failed %d \n", ret);
                        return ret;
                    }

                }

                // 往编码器中发送 AVFrame ,然后不断地 取AVPacket
                ret      = avcodec_send_frame(encodeContext, frame);
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
                        return ret;
                    }
                    //  case 3: ret >=0;
                    printf("pkt_out size : %d \n", pkt_out->size);
                    av_packet_unref(pkt_out);
                }

            } else {
                printf("other fail \n");
                return ret;

            }
        }


    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_packet_free(&pkt_out);

    //关闭编码器，解码器。
    avcodec_close(decodeContext);
    avcodec_close(encodeContext);

    //释放容器内存。
    avformat_free_context(avFormatContext);
    printf("done \n");


    return 0;
}

bool isAudioIndex(AVPacket *pPacket) {
    if (pPacket->stream_index == 1) {
        av_packet_unref(pPacket);
        return true;
    }
    return false;
}

int main2() {
    int ret = 0;
    int err;


    //打开输入文件
    char            filename[] = "juren-30s.mp4";
    AVFormatContext *fmt_ctx   = avformat_alloc_context();
    if (!fmt_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    if ((err = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        printf("can not open file %d \n", err);
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


    AVCodecContext *enc_ctx = NULL;


    AVPacket *pkt     = av_packet_alloc();
    AVFrame  *frame   = av_frame_alloc();
    AVPacket *pkt_out = av_packet_alloc();

    int frame_num = 0;
    int read_end  = 0;
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
        for (;;) {// for 2
            //读取 AVFrame
            ret = avcodec_receive_frame(avctx, frame);

            if (AVERROR(EAGAIN) == ret) {
                //提示 EAGAIN 代表 解码器 需要 更多的 AVPacket
                //跳出 第一层 for，让 解码器拿到更多的 AVPacket
                break;
            } else if (AVERROR_EOF == ret) {

                ret = avcodec_send_frame(enc_ctx, NULL);
                for (;;) {
                    ret = avcodec_receive_packet(enc_ctx, pkt_out);
                    //这里不可能返回 EAGAIN，如果有直接退出。
                    if (ret == AVERROR(EAGAIN)) {
                        printf("avcodec_receive_packet error code %d \n", ret);
                        return ret;
                    }
                    if (AVERROR_EOF == ret) {
                        break;
                    }
                    //编码出 AVPacket ，打印一些信息
                    printf("pkt_out size : %d \n", pkt_out->size);
                }

                //跳出 第二层 for，编码结束了。
                read_end = 1;
                break;
            } else if (ret >= 0) {
                //只有解码出来一个帧，才可以开始初始化编码器。
                if (NULL == enc_ctx) {
                    //打开编码器，并且设置 编码信息。
                    AVCodec *encode = avcodec_find_encoder(AV_CODEC_ID_H264);
                    enc_ctx = avcodec_alloc_context3(encode);
                    enc_ctx->codec_type             = AVMEDIA_TYPE_VIDEO;
                    enc_ctx->bit_rate               = 400000;
                    enc_ctx->framerate              = avctx->framerate;
                    enc_ctx->gop_size               = 10;
                    enc_ctx->max_b_frames           = 5;
                    enc_ctx->profile                = FF_PROFILE_H264_MAIN;
                    /*
                     * 其实下面这些信息在容器那里也有，也可以一开始直接在容器那里打开编码器
                     * 我从 AVFrame 里拿这些编码器参数是因为，容器的信息不一样就是最终的信息。
                     * 因为你解码出来的 AVFrame 可能会经过 filter 滤镜，经过滤镜之后信息就会变化，但是本文没有使用滤镜。
                     */
                    //编码器的时间基要取 AVFrame 的时间基，因为 AVFrame 是输入。
                    enc_ctx->time_base              = fmt_ctx->streams[0]->time_base;
                    enc_ctx->width                  = fmt_ctx->streams[0]->codecpar->width;
                    enc_ctx->height                 = fmt_ctx->streams[0]->codecpar->height;
                    enc_ctx->sample_aspect_ratio    = frame->sample_aspect_ratio;
                    enc_ctx->pix_fmt                = frame->format;
                    enc_ctx->color_range            = frame->color_range;
                    enc_ctx->color_primaries        = frame->color_primaries;
                    enc_ctx->color_trc              = frame->color_trc;
                    enc_ctx->colorspace             = frame->colorspace;
                    enc_ctx->chroma_sample_location = frame->chroma_location;

                    /* 注意，这个 field_order 不同的视频的值是不一样的，这里我写死了。
                     * 因为 本文的视频就是 AV_FIELD_PROGRESSIVE
                     * 生产环境要对不同的视频做处理的
                     */
                    enc_ctx->field_order = AV_FIELD_PROGRESSIVE;

                    if ((ret = avcodec_open2(enc_ctx, encode, NULL)) < 0) {
                        printf("open codec faile %d \n", ret);
                        return ret;
                    }
                }

                //往编码器发送 AVFrame，然后不断读取 AVPacket
                ret = avcodec_send_frame(enc_ctx, frame);

                if (ret < 0) {
                    printf("avcodec_send_frame fail %d \n", ret);
                    return ret;
                }
                for (;;) {
                    ret = avcodec_receive_packet(enc_ctx, pkt_out);
                    if (ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    //前面没有往 编码器发 NULL,所以正常情况 ret 不会小于 0
                    if (ret < 0) {
                        printf("avcodec_receive_packet fail %d \n", ret);
                        return ret;
                    }
                    //编码出 AVPacket ，打印一些信息。
                    printf("pkt_out size : %d \n", pkt_out->size);

                    av_packet_unref(pkt_out);
                }


            } else {
                printf("other fail \n");
                return ret;
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_packet_free(&pkt_out);

    //关闭编码器，解码器。
    avcodec_close(avctx);
    avcodec_close(enc_ctx);

    //释放容器内存。
    avformat_free_context(fmt_ctx);
    printf("done \n");

    return 0;
}
