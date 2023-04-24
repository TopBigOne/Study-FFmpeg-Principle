#include <stdio.h>

#include <libavformat/avformat.h>

#define INPUT_FILE_PATH "/Users/dev/Desktop/yuv_data/juren-30s.mp4"
//打开输入文件
char file_name_out[] = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/25_avio_output/doc/juren-30s.mp4";

AVFormatContext *input_av_format_context = NULL;
AVCodecContext  *decode_AVCodecContext   = NULL;
AVPacket        *input_av_packet         = NULL;
AVFrame         *input_av_frame          = NULL;


// 编码部分
AVFormatContext *output_av_format_ctx = NULL;
AVCodecContext  *encode_ctx           = NULL;

AVPacket *out_av_packet = NULL;
AVStream *st            = NULL;

int result = -1;
int err    = 0;

int read_end = 0;


int init_input_ffmpeg();

int init_output_ffmpeg();

int start_decode();


void set_output_packet_time_base();

void free_all();

int set_output_avformat_ctx();

int init_input_ffmpeg() {
    puts("init_input_ffmpeg");
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


int init_output_ffmpeg() {
    puts("init_output_ffmpeg");

    // todo 这个和 解码，不一样哦。。。。
    result = avformat_alloc_output_context2(&output_av_format_ctx, NULL, NULL, file_name_out);
    if (output_av_format_ctx == NULL) {
        return result;
    }
    // 添加一路流到 容器上下文
    st = avformat_new_stream(output_av_format_ctx, NULL);
    // 给输出流设置 time base
    st->time_base = input_av_format_context->streams[0]->time_base;

    set_output_avformat_ctx();


    return result;
}

int set_output_avformat_ctx() {
    if (encode_ctx != NULL) {
        return result;
    }

    puts("set_output_avformat_ctx()");
    AVCodec *avCodec = avcodec_find_encoder(AV_CODEC_ID_H264);


    encode_ctx = avcodec_alloc_context3(avCodec);


    encode_ctx->codec_type   = AVMEDIA_TYPE_VIDEO;
    // 码率
    encode_ctx->bit_rate     = 400000;
    // 帧率---> 从解码器上下文获取
    encode_ctx->framerate    = decode_AVCodecContext->framerate;
    // gop size
    encode_ctx->gop_size     = 30;
    encode_ctx->max_b_frames = 10;
    // 带有B帧 ，
    // FF_PROFILE_H264_BASELINE 没有B帧
    encode_ctx->profile      = FF_PROFILE_H264_MAIN;
    encode_ctx->time_base    = input_av_format_context->streams[0]->time_base;
    encode_ctx->height       = input_av_format_context->streams[0]->codecpar->height;
    encode_ctx->width        = input_av_format_context->streams[0]->codecpar->width;

    encode_ctx->sample_aspect_ratio    = st->sample_aspect_ratio = input_av_frame->sample_aspect_ratio;
    encode_ctx->pix_fmt                = decode_AVCodecContext->pix_fmt;
    // todo ? 这样，是否可行？？？ 需要断点调试一下。。这里没有使用滤镜
    encode_ctx->color_range            = input_av_frame->color_range;
    encode_ctx->color_primaries        = input_av_frame->color_primaries;
    encode_ctx->color_trc              = input_av_frame->color_trc;
    encode_ctx->colorspace             = input_av_frame->colorspace;
    encode_ctx->chroma_sample_location = input_av_frame->chroma_location;
    encode_ctx->field_order            = AV_FIELD_PROGRESSIVE;
    result = avcodec_parameters_from_context(st->codecpar, encode_ctx);

    if (result < 0) {
        printf("error code %d \n", result);
        return result;
    }


    result = avcodec_open2(encode_ctx, avCodec, NULL);
    if (result < 0) {
        printf("avcodec_open2() error is : %s \n", av_err2str(result));
        return result;
    }

    // 正式打开输出文件
    result = avio_open2(&output_av_format_ctx->pb, file_name_out, AVIO_FLAG_WRITE,
                        &output_av_format_ctx->interrupt_callback, NULL);
    if (result < 0) {
        printf("avio_open2() error code %d \n", result);
        return result;
    }


    // 写入头文件
    result = avformat_write_header(output_av_format_ctx, NULL);
    if (result < 0) {
        printf("avformat_write_header() error code %d \n", result);
        return result;
    }
    out_av_packet = av_packet_alloc();

    return result;


}


int start_decode() {
    puts("start_decode()");
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
                result = avcodec_send_frame(encode_ctx, NULL);
                for (;;) {
                    result = avcodec_receive_packet(encode_ctx, out_av_packet);
                    // case : 2-1
                    if (result == AVERROR(EAGAIN)) {
                        break;
                    }
                    // case : 2-2
                    if (result == AVERROR_EOF) {
                        printf("avcodec_receive_packet error code %d \n", result);
                        break;
                    }




                    // case 3:
                    if (result >= 0) {
                        printf("pkt_out out_av_packet : %d \n", out_av_packet->size);
                        set_output_packet_time_base();

                        result = av_interleaved_write_frame(output_av_format_ctx, out_av_packet);
                        if (result < 0) {
                            printf("av_interleaved_write_frame faile %d \n", result);
                            return result;
                        }
                        av_packet_unref(out_av_packet);

                    }
                }

                av_write_trailer(output_av_format_ctx);
                puts("read is end.");
                read_end = 1;
                break;
            }

            // case 3:
            if (result >= 0) {
                result = avcodec_send_frame(encode_ctx, input_av_frame);

                if (result < 0) {
                    printf("avcodec_send_frame fail %d \n", result);
                    return result;
                }

                for (;;) {
                    result = avcodec_receive_packet(encode_ctx, out_av_packet);
                    if (result == AVERROR(EAGAIN)) {
                        break;
                    }
                    if (result < 0) {
                        fprintf(stderr, " avcodec_receive_packet in ERROR, line: %d,err is : %s", __LINE__,
                                av_err2str(result));
                        return result;
                    }

                    set_output_packet_time_base();

                    result = av_interleaved_write_frame(output_av_format_ctx, out_av_packet);
                    if (result < 0) {
                        printf("av_interleaved_write_frame faile %d \n", result);
                        return result;
                    }

                    av_packet_unref(out_av_packet);
                }

                continue;
            }

            perror("other failure");
            return result;

        }
    }

    return result;

}

void set_output_packet_time_base() {
    //  puts("set_output_packet_time_base()");
    out_av_packet->stream_index             = st->index;
    // 转换AVPacket的 time base 为输出流的time base
    AVRational      input_ctx_time_base     = input_av_format_context->streams[0]->time_base;
    AVRational      output_stream_time_base = st->time_base;
    enum AVRounding avRounding              = AV_ROUND_INF | AV_ROUND_PASS_MINMAX;

    out_av_packet->pts = av_rescale_q_rnd(out_av_packet->pts, input_ctx_time_base,
                                          output_stream_time_base,
                                          avRounding);

    out_av_packet->dts      = av_rescale_q_rnd(out_av_packet->dts, input_ctx_time_base,
                                               output_stream_time_base,
                                               avRounding);
    out_av_packet->duration = av_rescale_q_rnd(out_av_packet->duration, input_ctx_time_base,
                                               output_stream_time_base,
                                               avRounding);
}


int main() {
    init_input_ffmpeg();
    init_output_ffmpeg();
    start_decode();
    free_all();
    return 0;
}

void free_all() {
    //关闭编码器，解码器。
    avcodec_close(decode_AVCodecContext);
    avcodec_close(encode_ctx);

    //释放容器内存。
    avformat_free_context(input_av_format_context);

    //必须调 avio_closep ，要不可能会没把数据写进去，会是 0kb
    avio_closep(&output_av_format_ctx->pb);
    avformat_free_context(output_av_format_ctx);
    printf("done \n");
}
