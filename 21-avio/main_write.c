//
// Created by dev on 2023/4/7.
//
#include <stdio.h>
#include <libavformat/avformat.h>

#define MIN(a, b) (a<b)?a:b

typedef struct _BufferData {
    uint8_t *ptr;           // 指向buffer 数据中还没 被io 上下文消耗的位置
    uint8_t *ori_ptr;       // 也是指向buffer 数据的指针，之所以定义ori_ptr ,是用在自定义seek 函数
    size_t  size;           // 视频buffer 还没被消耗部分的大小，随着不断消耗，越来越小
    size_t  file_size;      // 原始视频buffer的大小，也是用户自定义seek函数中
} BufferData;

/**
 * 把整个文件都读进内存
 * @param path
 * @param length
 * @return
 */
uint8_t *readFile(char *path, size_t *length) {
    FILE    *pfile;
    uint8_t *data;
    pfile = fopen(path, "rb");
    if (pfile == NULL) {
        return NULL;
    }
    fseek(pfile, 0, SEEK_END);
    *length = ftell(pfile);
    data = (uint8_t * )malloc((*length) * sizeof(uint8_t));
    rewind(pfile);
    *length = fread(data, 1, *length, pfile);
    fclose(pfile);
    return data;
}

/**
 * 读取AVPacket 回调函数
 * @param opaque
 * @param buf
 * @param buf_size
 * @return
 */
static int read_packet(void *opaque, uint8_t *buf, int buf_size) {
    BufferData *bufferData = (BufferData *) opaque;
    buf_size = MIN((int) bufferData->size, buf_size);
    if (!buf_size) {
        printf("no buffer size pass to read_packet ,%d,%zu\n", buf_size, bufferData->size);
        return -1;
    }
    // 数据copy
    memcpy(buf, bufferData->ptr, buf_size);
    bufferData->ptr += buf_size;
    bufferData->size -= buf_size; // left size in buffer.
    return buf_size;
}

static int write_packet(void *opaque, uint8_t *buf, int buf_size) {
    printf(" write size = %d\n", buf_size);
    uint8_t *output = (uint8_t *) opaque;
    memcpy(output, buf, buf_size);
    output += buf_size;
    return buf_size;
}

static int64_t seek_in_buffer(void *opaque, int64_t offset, int whence) {
    BufferData *bufferData = (BufferData *) opaque;
    int64_t    ret         = -1;
    printf("whence=%d, offset=%lld, file_size=%zu\n", whence, offset, bufferData->size);
    switch (whence) {
        // 不进行 seek 操作，而是返回 视频 buffer 整体的大小。也就是文件大小。
        case AVSEEK_SIZE:
            ret = (int64_t) bufferData->file_size;
            break;
            // 要进行 seek 操作，seek 到 offset 参数的位置，也就是需要 seek 到 第 offset 个字节。需要把 bd->ptr 指向第 offset 个字节
        case SEEK_SET:


            bufferData->ptr  = bufferData->ori_ptr + offset;
            bufferData->size = bufferData->file_size - offset;
            ret = (int64_t) bufferData->ptr;
            break;
        default:
            ret = -1;
    }
    return ret;
}

int text_main() {
    int             ret                  = 0;
    int             err                  = 0;
    uint8_t         *input;
    uint8_t         *output;
    AVFormatContext *avFormatContext             = NULL;
    AVIOContext     *avio_ctx            = NULL;
    uint8_t         *avio_ctx_buffer     = NULL;
    AVIOContext     *avio_ctx_out        = NULL;
    uint8_t         *avio_ctx_buffer_out = NULL;
    int             avio_ctx_buffer_size = 4096;
    size_t          file_len;
    BufferData      bufferData           = {0};
    char            filename[]           = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/21-avio/juren-30s.mp4";
    input = readFile(filename, &file_len);
    bufferData.ptr       = input;
    bufferData.ori_ptr   = input;
    bufferData.size      = file_len;
    bufferData.file_size = file_len;
    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    printf("avio_ctx_buffer is %p\n", avio_ctx_buffer);
    if (!avio_ctx_buffer) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }

    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0,
                                  &bufferData, &read_packet, NULL, &seek_in_buffer);
    if (!avio_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    avFormatContext->pb = avio_ctx;
    if ((err = avformat_open_input(&avFormatContext, NULL, NULL, NULL)) < 0) {
        printf("can not open file %d \n", err);
        return err;
    }

    ret = avformat_find_stream_info(avFormatContext, NULL);
    if (ret < 0) {
        printf("avformat_find_stream_info file %d \n", ret);
        return ret;
    }

    AVCodecContext *avCodecContext = avcodec_alloc_context3(NULL);
    ret = avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }
    AVCodec *codec = avcodec_find_decoder(avCodecContext->codec_id);
    if ((ret = avcodec_open2(avCodecContext, codec, NULL)) < 0) {
        printf("open codec faile %d \n", ret);
        return ret;
    }

    // 打开输出文件容器
    output              = av_malloc(1024 * 1024*100);
    avio_ctx_buffer_out = av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer_out) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    avio_ctx_out = avio_alloc_context(avio_ctx_buffer_out, avio_ctx_buffer_size, 1,
                                      (void *) output, NULL, &write_packet, NULL);
    if (!avio_ctx_out) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }

    AVFormatContext *fmt_ctx_out = NULL;
    err = avformat_alloc_output_context2(&fmt_ctx_out, NULL, "flv", NULL);
    if (!fmt_ctx_out) {
        printf("error code 33 %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    // todo 看看 avio_ctx_out 在哪里init的
    fmt_ctx_out->pb = avio_ctx_out;

    // 添加一路流到容器上下文
    AVStream *st = avformat_new_stream(fmt_ctx_out, NULL);
    st->time_base = avFormatContext->streams[0]->time_base;
    AVCodecContext *enc_ctx = NULL;
    AVPacket       *pkt     = av_packet_alloc();
    AVFrame        *frame   = av_frame_alloc();
    AVPacket       *pkt_out = av_packet_alloc();

    int read_end = 0;
    for (;;) {
        if (1 == read_end) {
            break;
        }

        ret = av_read_frame(avFormatContext, pkt);
        //跳过不处理音频包
        if (1 == pkt->stream_index) {
            av_packet_unref(pkt);
            continue;
        }
        if (AVERROR_EOF == ret) {
            //读取完文件，这时候 pkt 的 data 跟 size 应该是 null
            avcodec_send_packet(avCodecContext, NULL);
        } else {
            if (0 != ret) {
                printf("read error code %d, %s \n", ret, av_err2str(ret));
                return ENOMEM;
            }
            retry:
            if (avcodec_send_packet(avCodecContext, pkt) == AVERROR(EAGAIN)) {
                printf("Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                //这里可以考虑休眠 0.1 秒，返回 EAGAIN 通常是 ffmpeg 的内部 api 有bug
                goto retry;
            }
            //释放 pkt 里面的编码数据
            av_packet_unref(pkt);

        }

        for (;;) {
            ret = avcodec_receive_frame(avCodecContext, frame);
            if (AVERROR(EAGAIN) == ret) {
                //提示 EAGAIN 代表 解码器 需要 更多的 AVPacket
                //跳出 第一层 for，让 解码器拿到更多的 AVPacket
                break;
            }
            if (AVERROR_EOF == ret) {
                // 往编码器发送 null 的 AVFrame，让编码器把剩下的数据刷出来。
                ret = avcodec_send_frame(enc_ctx, NULL);
                for (;;) {
                    // 编码器
                    ret = avcodec_receive_packet(enc_ctx, pkt_out);
                    if (ret == AVERROR(EAGAIN)) {
                        printf("avcodec_receive_packet error code %d \n", ret);
                        return ret;
                    }
                    if (AVERROR_EOF == ret) {
                        break;
                    }
                    // 编码出AVPacket ，先打印一些信息，然后把它写入文件
                    printf("pkt_out size : %d\n", pkt_out->size);
                    // 设置AVPacket的stream_index ,这样才知道是哪一个流
                    pkt_out->stream_index = st->index;

                    // 转换AVPacket的时间基为输出流的时间基
                    pkt_out->pts      = av_rescale_q_rnd(pkt_out->pts, avFormatContext->streams[0]->time_base,
                                                         st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                    pkt_out->dts      = av_rescale_q_rnd(pkt_out->dts, avFormatContext->streams[0]->time_base,
                                                         st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                    pkt_out->duration = av_rescale_q_rnd(pkt_out->duration, avFormatContext->streams[0]->time_base,
                                                         st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                    ret = av_interleaved_write_frame(avFormatContext, pkt_out);
                    if (ret < 0) {
                        printf("av_interleaved_write_frame faile %d \n", ret);
                        return ret;

                    }
                    av_packet_unref(pkt_out);
                }
                av_write_trailer(fmt_ctx_out);
                read_end = 1;
                break;

            }
            if (ret >= 0) {
                // 只有解码出来 一个帧，才可以初始化编码器
                if (enc_ctx == NULL) {
                    AVCodec *encodec = avcodec_find_encoder(AV_CODEC_ID_H264);
                    enc_ctx = avcodec_alloc_context3(encodec);
                    enc_ctx->codec_type             = AVMEDIA_TYPE_VIDEO;
                    enc_ctx->bit_rate               = 400000;
                    enc_ctx->framerate              = avCodecContext->framerate;
                    enc_ctx->gop_size               = 30;
                    enc_ctx->max_b_frames           = 10;
                    enc_ctx->profile                = FF_PROFILE_H264_MAIN;
                    // 其实下面这些信息在容器哪里也有，也可以一开始在容器哪里打开编码器
                    // 我从AVFrame 里拿这些编码参数是因为：容器的不一样及时最终的。
                    // 因为你解码出来的AVFrame可能会经过filter滤镜，经过滤镜之后信息就会变换
                    // 但是本文没有使用滤镜
                    // 编码器的时间基 要取AVFrame 的时间基，因为AVFrame 是输入，AVFrame 的时间基就是流的时间基。
                    enc_ctx->time_base              = avFormatContext->streams[0]->time_base;
                    enc_ctx->width                  = avFormatContext->streams[0]->codecpar->width;
                    enc_ctx->height                 = avFormatContext->streams[0]->codecpar->height;
                    enc_ctx->sample_aspect_ratio    = st->sample_aspect_ratio = frame->sample_aspect_ratio;
                    enc_ctx->pix_fmt                = frame->format;
                    enc_ctx->color_range            = frame->color_range;
                    enc_ctx->color_primaries        = frame->color_primaries;
                    enc_ctx->color_trc              = frame->color_trc;
                    enc_ctx->colorspace             = frame->colorspace;
                    enc_ctx->chroma_sample_location = frame->chroma_location;

                    // NOTE : filed_order ，不同的视频的值是不一样的，生产环境，要动态处理一下
                    enc_ctx->field_order = AV_FIELD_PROGRESSIVE;

                    // 现在我们需要把 编码器参数 copy 给流，解码的时候，是从流赋值参数给解码器
                    // 现在要反着来
                    ret = avcodec_parameters_from_context(st->codecpar, enc_ctx);
                    if (ret < 0) {
                        printf("error code %d \n", ret);
                        return ret;
                    }
                    if ((ret = avcodec_open2(enc_ctx, encodec, NULL)) < 0) {
                        printf("open codec faile %d \n", ret);
                        return ret;

                    }
                    ret = avformat_write_header(fmt_ctx_out, NULL);
                    if (ret < 0) {
                        printf("avformat_write_header fail %d \n", ret);
                        return ret;
                    }
                }
                // 往编码器发送AVFrame ，然后不断读取AVPacket
                ret = avcodec_send_frame(enc_ctx, frame);
                if (ret < 0) {
                    fprintf(stderr,"avcodec_send_frame fail: %s \n", av_err2str(ret));
                    return ret;
                }
                // loop 编码
                for (;;) {
                    ret = avcodec_receive_packet(enc_ctx, pkt_out);
                    if (ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    if (ret < 0) {
                        printf("avcodec_receive_packet fail %d \n", ret);
                        return ret;
                    }
                    //编码出 AVPacket ，先打印一些信息，然后把它写入文件。
                    printf("编码出 AVPacket pkt_out size : %d \n",pkt_out->size);

                    //设置 AVPacket 的 stream_index ，这样才知道是哪个流的。
                    pkt_out->stream_index = st->index;
                    //转换 AVPacket 的时间基为 输出流的时间基。
                    pkt_out->pts          = av_rescale_q_rnd(pkt_out->pts, avFormatContext->streams[0]->time_base,
                                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                    pkt_out->dts          = av_rescale_q_rnd(pkt_out->dts, avFormatContext->streams[0]->time_base,
                                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                    pkt_out->duration     = av_rescale_q_rnd(pkt_out->duration, avFormatContext->streams[0]->time_base,
                                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

                    //
                    ret = av_interleaved_write_frame(fmt_ctx_out, pkt_out);
                    if (ret < 0) {
                        printf("av_interleaved_write_frame faile %d \n", ret);
                        return ret;
                    }
                    // 为了复用
                    av_packet_unref(pkt_out);
                }
                continue;

            }
            printf("other fail \n");
            return ret;
        }
    }


    av_free(&avio_ctx_buffer);
    avio_context_free(&avio_ctx);
    av_free(&avio_ctx_buffer_out);
    avio_context_free(&avio_ctx_out);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_packet_free(&pkt_out);

    //关闭编码器，解码器。
    avcodec_close(avCodecContext);
    avcodec_close(enc_ctx);

    //释放容器内存。
    avformat_free_context(avFormatContext);
    avformat_free_context(fmt_ctx_out);
    printf("------- done --------- \n");

    return 0;

}


