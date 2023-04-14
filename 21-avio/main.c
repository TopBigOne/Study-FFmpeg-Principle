#include <stdio.h>

#include <libavformat/avformat.h>

#define MIN(a, b) (a<b) ? a:b

typedef struct _BufferData {
    uint8_t *ptr;       // 指向buffer数据中，还没被io上下文消耗的位置
    uint8_t *ori_ptr;   // 也是指向buffer数据的指针，之所以 定义ori_ptr,是用在自定义seek函数中；
    size_t  size;
    size_t  file_size;
} BufferData;

#define SIZE_4KB 4096

AVPacket       *pkt            = NULL;
AVFrame        *frame          = NULL;
AVPacket       *pkt_out        = NULL;
AVCodecContext *encodec_cxt    = NULL;
AVCodecContext *avCodecContext = NULL;


BufferData bufferData = {0};

char filename_out[] = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/21-avio/out/juren-30s-5.mp4";

int init_encoder_context();

uint8_t *readFile(char *name, size_t *pInt);

static int read_packet(void *opaque, uint8_t *buf, int buf_size) {
    BufferData *bd = (BufferData *) opaque;
    buf_size = MIN((int) bd->size, buf_size);
    if (!buf_size) {
        printf("no buf_size pass to read_packet,%d,%zu\n", buf_size, bd->size);
        return EAGAIN;
    }
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr += buf_size;
    bd->size -= buf_size;
    return buf_size;
}

static int64_t seek_in_buffer(void *opaque, int64_t offset, int whence) {
    BufferData *bd    = (BufferData *) opaque;
    int64_t    result = -1;

    switch (whence) {
        case AVSEEK_SIZE:
            result = bd->file_size;
            break;
        case SEEK_SET:
            bd->ptr  = bd->ori_ptr + offset;
            bd->size = bd->file_size - offset;
            result = (int64_t) bd->ptr;
            break;
    }
    return result;
}

int start_read_file() {
    puts("start_read_file:开始读取本地的mp4 文件.");
    char    *file_name = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/21-avio/juren-30s.mp4";
    uint8_t *input;
    size_t  file_size;
    input = readFile(file_name, &file_size);
    puts("为BufferData 的属性赋值.");
    bufferData.ptr       = input;
    bufferData.ori_ptr   = input;
    bufferData.size      = file_size;
    bufferData.file_size = file_size;
    return 0;
}

uint8_t *readFile(char *file_path, size_t *file_size) {
    uint8_t *data = NULL;
    FILE    *fp   = NULL;
    fp = fopen(file_path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    // 给指针 赋值
    *file_size = ftell(fp);
    // 申请一片内存
    data = malloc((*file_size) * sizeof(uint8_t));
    rewind(fp);

    *file_size = fread(data, 1, *file_size, fp);
    fclose(fp);

    return data;
}


AVFormatContext *avFormatContext = NULL;
AVIOContext     *avio_ctx        = NULL;
AVInputFormat   *avInputFormat   = NULL;
uint8_t         *avio_ctx_buffer = NULL;

AVFormatContext *fmt_out_ctx = NULL;
AVStream        *out_stream  = NULL;


void init_output_ffmpeg() {
    puts("init_output_ffmpeg");
    int result = -1;

    result = avformat_alloc_output_context2(&fmt_out_ctx, NULL, NULL, filename_out);
    if (result < 0) {
        return;
    }
    out_stream = avformat_new_stream(fmt_out_ctx, NULL);
    // 设置输出流的时间基
    out_stream->time_base = avFormatContext->streams[0]->time_base;
    pkt_out = av_packet_alloc();
}


int init_avio_context() {
    puts("init_avio_context: the core function is avio_alloc_context.");
    avInputFormat   = av_find_input_format("mp4");
    avio_ctx_buffer = av_malloc(SIZE_4KB);
    if (avio_ctx_buffer == NULL) {
        perror("av_malloc in error.");
        return ENOMEM;
    }
    avio_ctx = avio_alloc_context(avio_ctx_buffer,
                                  SIZE_4KB,
                                  0,
                                  &bufferData, &read_packet, NULL, &seek_in_buffer);
    if (avio_ctx == NULL) {
        return -1;
    }
    return 0;
}


/**
 * 初始换，传递一段内存数据
 */
void init_input_ffmpeg() {
    int result = 1;
    puts("init_input_ffmpeg");
    avFormatContext = avformat_alloc_context();
    if (avFormatContext == NULL) {
        return;
    }

    avFormatContext->pb = avio_ctx;

    result = avformat_open_input(&avFormatContext, NULL, avInputFormat, NULL);
    if (result < 0) {
        return;
    }

    result = avformat_find_stream_info(avFormatContext, NULL);
    if (result < 0) {
        return;
    }
    avCodecContext = avcodec_alloc_context3(NULL);
    result         = avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
    if (result < 0) {
        return;
    }
    AVCodec *avCodec = avcodec_find_decoder(avCodecContext->codec_id);

    result = avcodec_open2(avCodecContext, avCodec, NULL);
    if (result < 0) {
        return;
    }

    pkt   = av_packet_alloc();
    frame = av_frame_alloc();
}


int init_encoder_context() {
    int result = -1;
    if (encodec_cxt != NULL) {
        return result;
    }
    puts("init_encoder_context");
    AVCodec *encode = avcodec_find_encoder(AV_CODEC_ID_H264);
    encodec_cxt = avcodec_alloc_context3(encode);

    encodec_cxt->codec_type   = AVMEDIA_TYPE_VIDEO;
    encodec_cxt->bit_rate     = 9000000;// 码率，可以改变视频的大小哦；
    encodec_cxt->framerate    = avCodecContext->framerate;
    encodec_cxt->gop_size     = 30;
    encodec_cxt->max_b_frames = 10;
    encodec_cxt->profile      = FF_PROFILE_H264_MAIN;

    encodec_cxt->time_base           = avFormatContext->streams[0]->time_base;
    encodec_cxt->width               = avFormatContext->streams[0]->codecpar->width;
    encodec_cxt->height              = avFormatContext->streams[0]->codecpar->height;
    encodec_cxt->sample_aspect_ratio = out_stream->sample_aspect_ratio = frame->sample_aspect_ratio;
    encodec_cxt->pix_fmt             = frame->format;

    encodec_cxt->color_range            = frame->color_range;
    encodec_cxt->color_primaries        = frame->color_primaries;
    encodec_cxt->color_trc              = frame->color_trc;
    encodec_cxt->colorspace             = frame->colorspace;
    encodec_cxt->chroma_sample_location = frame->chroma_location;

    encodec_cxt->field_order = AV_FIELD_PROGRESSIVE;
    // 把流的参数，赋值给解码器
    result = avcodec_parameters_from_context(out_stream->codecpar, encodec_cxt);
    if (result < 0) {
        printf("error code %d \n", result);
        return result;
    }
    result = avcodec_open2(encodec_cxt, encode, NULL);
    if (result < 0) {
        printf("avcodec_open2 in ERROR. %d \n", result);
        return result;
    }

    if ((result = avio_open2(&fmt_out_ctx->pb, filename_out,
                             AVIO_FLAG_WRITE, &fmt_out_ctx->interrupt_callback, NULL)) <
        0) {
        printf("avio_open2 faile %d \n", result);
        return result;
    }

    // write header
    puts("init_encoder_context-----------------write mp4 header.");
    result = avformat_write_header(fmt_out_ctx, NULL);
    if (result < 0) {
        printf("avformat_write_header %d \n", result);
        return result;
    }
    return result;
}


/**
 * 核心函数 ： av_interleaved_write_frame()
 * @return
 */
int start_write_output_packet() {
    puts("start_write_output_packet");
    pkt_out->stream_index                   = out_stream->index;
    // 转换时间基
    AVRational      origin_time_base        = avFormatContext->streams[0]->time_base;
    AVRational      output_stream_time_base = out_stream->time_base;
    enum AVRounding rounding                = AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX;

    pkt_out->pts      = av_rescale_q_rnd(pkt_out->pts, origin_time_base, output_stream_time_base, rounding);
    pkt_out->dts      = av_rescale_q_rnd(pkt_out->dts, origin_time_base, output_stream_time_base, rounding);
    pkt_out->duration = av_rescale_q_rnd(pkt_out->duration, origin_time_base, output_stream_time_base, rounding);

    return av_interleaved_write_frame(fmt_out_ctx, pkt_out);
}

void start_encode_output_packet() {
    puts("start_encode_output_packet");
    int result = -1;
    avcodec_send_frame(encodec_cxt, NULL);
    for (;;) {
        result = avcodec_receive_packet(encodec_cxt, pkt_out);
        if (result == AVERROR_EOF) {
            break;
        }
        if (result == AVERROR(EAGAIN)) {
            return;
        }
        result = start_write_output_packet();
        if (result < 0) {
            return;
        }
        av_packet_unref(pkt_out);
    }
    puts("start_encode_output_packet-----------------write mp4 trailer.");
    av_write_trailer(fmt_out_ctx);
}

void start_decode_memory_data() {
    puts("start_decode_memory_data");
    int read_end = 0;
    int result   = -1;
    for (;;) {
        if (read_end == 1) {
            break;
        }
        result = av_read_frame(avFormatContext, pkt);
        if (pkt->stream_index == 1) {
            av_packet_unref(pkt);
            continue;
        }
        if (result == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);
        } else {
            if (result != 0) {
                return;
            }
            TAG_RETRY:
            if ((avcodec_send_packet(avCodecContext, pkt)) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
            av_packet_unref(pkt);
        }

        for (;;) {
            result = avcodec_receive_frame(avCodecContext, frame);
            if (result == AVERROR(EAGAIN)) {
                break;
            }
            if (result == AVERROR_EOF) {
                start_encode_output_packet();
                puts("跳出第二层for,文件解码已经完毕");
                read_end = 1;
                break;
            }
            if (result >= 0) {
                init_encoder_context();
                result = avcodec_send_frame(encodec_cxt, frame);
                if (result < 0) {
                    fprintf(stderr, "avcodec_send_frame in ERROR : %s", av_err2str(result));
                    return;
                }

                for (;;) {
                    result = avcodec_receive_packet(encodec_cxt, pkt_out);
                    if (result == AVERROR(EAGAIN)) {
                        break;
                    }
                    if (result < 0) {
                        return;
                    }
                    // 往 AVPacket 中写数据
                    result = start_write_output_packet();
                    if (result < 0) {
                        return;
                    }
                    av_packet_unref(pkt_out);
                }

                continue;

            }
            perror("未知错误");
            return;
        }

    }

}

void free_all() {
    // todo : 把时间花在刀刃上.....

}

int my_code() {
    int result = 0;
    start_read_file();
    init_avio_context();
    init_input_ffmpeg();
    init_output_ffmpeg();
    start_decode_memory_data();
    free_all();
    return 0;
}

int main() {
    my_code();
    return 0;
}

