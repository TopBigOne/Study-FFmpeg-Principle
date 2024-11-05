#include <stdio.h>

#include <libavformat/avformat.h>


#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>


AVFormatContext *input_av_format_context = NULL;
AVCodecContext  *decode_AVCodecContext   = NULL;
AVPacket        *input_av_packet         = NULL;
AVFrame         *input_av_frame          = NULL;


int result = -1;
int err    = 0;

int read_end = 0;

#define INPUT_FILE_PATH_1 "/Users/dev/Desktop/mp4/红装.mp4"
#define INPUT_FILE_PATH_2 "/Users/dev/Desktop/mp4/不如.mp4"
#define INPUT_FILE_PATH_3 "https://gw.alicdn.com/bao/uploaded/LB1l2iXISzqK1RjSZFjXXblCFXa.mp4?file=LB1l2iXISzqK1RjSZFjXXblCFXa.mp4"
#define INPUT_FILE_PATH INPUT_FILE_PATH_2

#define PATH_OUTPUT_PNG "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/33_frameToImage/DOC/2345_erere.png"

int init_input_ffmpeg();

int start_decode();

void saveFrameToJpg(AVFrame *frame, const char *path);

int  ff_convert_to_image(AVFrame *frame, const char *path);


int frameToImage(AVFrame *frame, enum AVCodecID codecID, uint8_t *outbuf, size_t outbufSize);

int count   = 0;
int c_limit = 16;

int main() {
    result = init_input_ffmpeg();
    if (result < 0) {
        perror(" error 1.\n");
        return 0;
    }

    result = start_decode();


    return 0;
}

int start_decode() {
    puts(__func__);
    for (;;) {
        if (read_end == 1) {
            break;
        }
        result = av_read_frame(input_av_format_context, input_av_packet);
        if (input_av_packet->stream_index == 1) {
            av_packet_unref(input_av_packet);
            continue;
        }
        if (result == AVERROR_EOF) {
            avcodec_send_packet(decode_AVCodecContext, NULL);
        } else {
            if (result != 0) {
                return result;
            }
            TAG_RETRY:
            if (avcodec_send_packet(decode_AVCodecContext, input_av_packet) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
            av_packet_unref(input_av_packet);
        }



        // 第二层： 解码loop
        for (;;) {
            result = avcodec_receive_frame(decode_AVCodecContext, input_av_frame);

            // case 1:
            if (result == AVERROR(EAGAIN)) {
                break;
            }

            //case 2:
            if (result == AVERROR_EOF) {
                read_end = 1;
                break;
            }
            // case 3:
            if (result >= 0) {

                // saveFrameToJpg(input_av_frame, PATH_OUTPUT_PNG);
                ff_convert_to_image(input_av_frame, PATH_OUTPUT_PNG);

                if (count == c_limit) {
                    // count=0;

                   //  saveFrameToJpg(input_av_frame, PATH_OUTPUT_PNG);

                } else {
                    count++;
                }


                continue;
            }


        }


        perror("other failure");
        return result;

    }


    return result;
}


int init_input_ffmpeg() {
    puts(__func__);
    input_av_format_context = avformat_alloc_context();

    avformat_open_input(&input_av_format_context, INPUT_FILE_PATH, NULL, NULL);

    avformat_find_stream_info(input_av_format_context, NULL);

    decode_AVCodecContext = avcodec_alloc_context3(NULL);

    avcodec_parameters_to_context(decode_AVCodecContext, input_av_format_context->streams[0]->codecpar);

    AVCodec *avCodec = avcodec_find_decoder(decode_AVCodecContext->codec_id);

    result = avcodec_open2(decode_AVCodecContext, avCodec, NULL);

    input_av_packet = av_packet_alloc();
    input_av_frame  = av_frame_alloc();

    return result;

}


void saveFrameToJpg(AVFrame *frame, const char *path) {
    puts(__func__);
    //确保缓冲区长度大于图片,使用brga像素格式计算。如果是bmp或tiff依然可能超出长度，需要加一个头部长度，或直接乘以2。
    int     bufSize = av_image_get_buffer_size(AV_PIX_FMT_BGRA, frame->width, frame->height, 64);
    //申请缓冲区
    uint8_t *buf    = (uint8_t *) av_malloc(bufSize);
    //将视频帧转换成jpg图片，如果需要png则使用 AV_CODEC_ID_PNG
    //int     picSize = frameToImage(frame, AV_CODEC_ID_MJPEG, buf, bufSize);
    int     picSize = frameToImage(frame, AV_CODEC_ID_PNG, buf, bufSize);
    //写入文件
    FILE    *f      = fopen(path, "wb+");
    if (f) {
        fwrite(buf, sizeof(uint8_t), bufSize, f);
        fclose(f);
    }
    //释放缓冲区
    av_free(buf);
}


int frameToImage(AVFrame *frame, enum AVCodecID codecID, uint8_t *outbuf, size_t outbufSize) {
    puts(__func__);
    int               ret         = 0;
    AVPacket          pkt;
    AVCodec           *codec;
    AVCodecContext    *ctx        = NULL;
    AVFrame           *rgbFrame   = NULL;
    uint8_t           *buffer     = NULL;
    struct SwsContext *swsContext = NULL;
    av_init_packet(&pkt);
    codec = avcodec_find_encoder(codecID);
    if (!codec) {
        printf("avcodec_send_frame error %d", codecID);
        goto IMAGE_END;
    }
    if (!codec->pix_fmts) {
        printf("unsupport pix format with codec %s", codec->name);
        goto IMAGE_END;
    }
    ctx = avcodec_alloc_context3(codec);
    ctx->bit_rate      = 3000000;
    ctx->width         = frame->width;
    ctx->height        = frame->height;
    ctx->time_base.num = 1;
    ctx->time_base.den = 25;
    ctx->gop_size      = 10;
    ctx->max_b_frames  = 0;
    ctx->thread_count  = 1;
    ctx->pix_fmt       = *codec->pix_fmts;
    ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0) {
        printf("avcodec_open2 error %d", ret);
        goto IMAGE_END;
    }
    if (frame->format != ctx->pix_fmt) {
        rgbFrame = av_frame_alloc();
        if (rgbFrame == NULL) {
            printf("av_frame_alloc  fail");
            goto IMAGE_END;
        }
        swsContext = sws_getContext(frame->width, frame->height, (enum AVPixelFormat) frame->format, frame->width,
                                    frame->height, ctx->pix_fmt, 1, NULL, NULL, NULL);
        if (!swsContext) {
            printf("sws_getContext  fail");
            goto IMAGE_END;
        }
        int bufferSize = av_image_get_buffer_size(ctx->pix_fmt, frame->width, frame->height, 1) * 2;
        buffer = (unsigned char *) av_malloc(bufferSize);
        if (buffer == NULL) {
            printf("buffer alloc fail:%d", bufferSize);
            goto IMAGE_END;
        }
        av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, ctx->pix_fmt, frame->width, frame->height, 1);
        if ((ret = sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, rgbFrame->data,
                             rgbFrame->linesize)) < 0) {
            printf("sws_scale error %d", ret);
        }
        rgbFrame->format = ctx->pix_fmt;
        rgbFrame->width  = ctx->width;
        rgbFrame->height = ctx->height;
        ret = avcodec_send_frame(ctx, rgbFrame);
    } else {
        ret = avcodec_send_frame(ctx, frame);
    }
    if (ret < 0) {
        printf("avcodec_send_frame error %d", ret);
        goto IMAGE_END;
    }
    ret = avcodec_receive_packet(ctx, &pkt);
    if (ret < 0) {
        printf("avcodec_receive_packet error %d", ret);
        goto IMAGE_END;
    }
    if (pkt.size > 0 && pkt.size <= outbufSize)
        memcpy(outbuf, pkt.data, pkt.size);
    ret = pkt.size;

    IMAGE_END:
    if (swsContext) {
        sws_freeContext(swsContext);
    }
    if (rgbFrame) {
        av_frame_unref(rgbFrame);
        av_frame_free(&rgbFrame);
    }
    if (buffer) {
        av_free(buffer);
    }
    av_packet_unref(&pkt);
    if (ctx) {
        avcodec_close(ctx);
        avcodec_free_context(&ctx);
    }
    return ret;
}



int ff_convert_to_image(AVFrame *frame,const char *path) {
    puts(__func__);
    int               ret         = 0;
    AVPacket          *pkt;
    AVCodec           *codec;
    AVCodecContext    *ctx        = NULL;
    AVFrame           *rgbFrame   = NULL;
    uint8_t           *buffer     = NULL;
    struct SwsContext *swsContext = NULL;

    int     out_buf_Size = av_image_get_buffer_size(AV_PIX_FMT_BGRA, frame->width, frame->height, 64);
    uint8_t *out_buf     = (uint8_t *) av_malloc(out_buf_Size);

    pkt = av_packet_alloc();
    // av_init_packet(&pkt);
    codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!codec) {
        printf("avcodec_send_frame error %d", AV_CODEC_ID_PNG);
        goto IMAGE_END;
    }
    if (!codec->pix_fmts) {
        printf("unsupport pix format with codec %s", codec->name);
        goto IMAGE_END;
    }
    ctx = avcodec_alloc_context3(codec);
    ctx->bit_rate      = 3000000;
    ctx->width         = frame->width;
    ctx->height        = frame->height;
    ctx->time_base.num = 1;
    ctx->time_base.den = 25;
    ctx->gop_size      = 10;
    ctx->max_b_frames  = 0;
    ctx->thread_count  = 1;
    ctx->pix_fmt       = *codec->pix_fmts;
    ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0) {
        printf("avcodec_open2 error %d", ret);
        goto IMAGE_END;
    }
    if (frame->format != ctx->pix_fmt) {
        rgbFrame = av_frame_alloc();
        if (rgbFrame == NULL) {
            printf("av_frame_alloc  fail");
            goto IMAGE_END;
        }
        swsContext = sws_getContext(frame->width, frame->height, (enum AVPixelFormat) frame->format, frame->width,
                                    frame->height, ctx->pix_fmt, 1, NULL, NULL, NULL);
        if (!swsContext) {
            printf("sws_getContext  fail");
            goto IMAGE_END;
        }
        int bufferSize = av_image_get_buffer_size(ctx->pix_fmt, frame->width, frame->height, 1) * 2;
        buffer = (unsigned char *) av_malloc(bufferSize);
        if (buffer == NULL) {
            printf("buffer alloc fail:%d", bufferSize);
            goto IMAGE_END;
        }
        av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, ctx->pix_fmt, frame->width, frame->height, 1);
        if ((ret = sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, rgbFrame->data,
                             rgbFrame->linesize)) < 0) {
            printf("sws_scale error %d", ret);
        }
        rgbFrame->format = ctx->pix_fmt;
        rgbFrame->width  = ctx->width;
        rgbFrame->height = ctx->height;
        ret = avcodec_send_frame(ctx, rgbFrame);
    } else {
        ret = avcodec_send_frame(ctx, frame);
    }
    if (ret < 0) {
        printf("avcodec_send_frame error %d", ret);
        goto IMAGE_END;
    }
    ret = avcodec_receive_packet(ctx, pkt);
    if (ret < 0) {
        printf("avcodec_receive_packet error %d", ret);
        goto IMAGE_END;
    }
    if (pkt->size > 0 && pkt->size <= out_buf_Size)
        memcpy(out_buf, pkt->data, pkt->size);
    ret = pkt->size;

    if(ret>0){
        FILE    *f      = fopen(path, "wb+");
        if (f) {
            fwrite(out_buf, sizeof(uint8_t), out_buf_Size, f);
            fclose(f);
        }
        //释放缓冲区
        av_free(out_buf);
        goto IMAGE_END;
    }



    IMAGE_END:
    if (swsContext) {
        sws_freeContext(swsContext);
    }
    if (rgbFrame) {
        av_frame_unref(rgbFrame);
        av_frame_free(&rgbFrame);
    }
    if (buffer) {
        av_free(buffer);
    }
    if(pkt){
        av_packet_free(&pkt);
    }
    if (ctx) {
        avcodec_close(ctx);
        avcodec_free_context(&ctx);
    }





    return ret;
}


