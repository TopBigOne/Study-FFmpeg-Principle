#include <stdio.h>
#include "libavformat/avformat.h"

#define MIN(a, b) (a<b)?a:b
typedef struct _BufferData {
    uint8_t *ptr;      // 指向buffer数据中 "还没被io上下文消耗的位置"
    uint8_t *ori_ptr;   // 也是指向buffer数据的指针,之所以定义ori_ptr,是用在自定义seek函数中
    size_t  size;       // 视频buffer还没被消耗部分的大小,随着不断消耗,越来越小
    size_t  file_size;  //原始视频buffer的大小,也是用在自定义seek函数中
} BufferData;


// 全局变量部分
BufferData      bd               = {0};
AVCodecContext  *avctx           = NULL;
uint8_t         *avio_ctx_buffer = NULL;
AVIOContext     *avio_ctx                = NULL;
AVFormatContext *input_av_format_context = NULL;

AVStream        *st          = NULL;
AVFormatContext *fmt_ctx_out = NULL;
AVCodecContext  *enc_ctx     = NULL;

AVPacket *pkt     = NULL;
AVFrame  *frame   = NULL;
AVPacket *pkt_out = NULL;


AVInputFormat *avInputFormat = NULL;

// outcome video name
char filename_out[] = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/21-avio-main-luo/output/juren-30s-3.mp4";


#define AVIO_BUFFER_SIZE 4096


int init_input_ffmpeg();

int init_output_ffmpeg();

int init_encode_context();

void start_read_file();

void free_all();

int start_write_packet();

int start_receive_packet();

/* 把整个文件的内容全部读进去内存 */
uint8_t *readFile(char *path, size_t *length);

int init_avio_context();

void print_avpacket_info(AVPacket *temp_packet);

/**
 * 读取 AVPacket 回调函数
 * @param opaque     原数据；
 * @param des_buf
 * @param buf_size
 * @return
 */
static int read_packet(void *opaque, uint8_t *des_buf, int buf_size) {
    puts("     read_packet ");
    BufferData *bd = (BufferData *) opaque;
    //
    buf_size = MIN((int) bd->size, buf_size); // buf_size : 4kb

    printf("     ***** des_buf pointer address is : %p \n", des_buf);
    if (!buf_size) {
        printf("no buf_size pass to read_packet,%d,%zu\n", buf_size, bd->size);
        return EAGAIN;
    }
    //printf("ptr in file:%p io.buffer ptr:%p, size:%zu,buf_size:%d\n", bd->ptr, des_buf, bd->size, buf_size);
    memcpy(des_buf, bd->ptr, buf_size);

    bd->ptr += buf_size;
    bd->size -= buf_size; // left size in buffer
    return buf_size;
}

/* seek 回调函数 */
static int64_t seek_in_buffer(void *opaque, int64_t offset, int whence) {
    BufferData *bd = (BufferData *) opaque;
    int64_t    ret = -1;


    printf("    seek_in_buffer() : whence=%d , offset=%lld , file_size=%zu\n", whence, offset, bd->file_size);
    switch (whence) {
        case AVSEEK_SIZE:
            ret = bd->file_size;
            break;
        case SEEK_SET:
            bd->ptr  = bd->ori_ptr + offset;
            bd->size = bd->file_size - offset;
            ret = (int64_t) bd->ptr;
            break;
    }
    return ret;
}


void start_read_file() {
    puts("1:开始读取本地的mp4文件");
    uint8_t *input;
    char    filename[] = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/21-avio-main-luo/juren-30s.mp4";
    size_t  file_len;
    // start ....
    input = readFile(filename, &file_len);
    puts("为 BufferData 的属性赋值");
    bd.ptr       = input;
    bd.ori_ptr   = input;
    bd.size      = file_len;
    bd.file_size = file_len;
}

uint8_t *readFile(char *path, size_t *length) {
    FILE *pfile;

    pfile = fopen(path, "rb");

    uint8_t *data; // todo ??

    if (pfile == NULL)
        return NULL;
    fseek(pfile, 0, SEEK_END);

    // *length VS length

    *length = ftell(pfile);

    data = (uint8_t *) malloc((*length) * sizeof(uint8_t));
    rewind(pfile);
    //   uint8_t VS int
    *length = fread(data, sizeof(uint8_t), *length, pfile);
    fclose(pfile);

    return data;
}

int init_input_ffmpeg() {
    int ret = -1;
    puts("3: init_input_ffmpeg");
    //打开输入文件
    input_av_format_context = avformat_alloc_context();
    if (!input_av_format_context) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }


    //avio_ctx->seekable = 0;
    input_av_format_context->pb = avio_ctx;

    if ((ret = avformat_open_input(&input_av_format_context, NULL, avInputFormat, NULL)) < 0) {
        printf("can not open file %d \n", ret);
        return ret;
    }
    double total_seconds = input_av_format_context->duration * av_q2d(AV_TIME_BASE_Q);
    puts("|-----------------------------------------|");
    printf("| 原始视频总时长为：%lld um\n", input_av_format_context->duration);
    printf("| 原始视频总时长为：%f s\n", total_seconds);
    puts("|-----------------------------------------|\n");
    AVRational video_time_base = input_av_format_context->streams[0]->time_base;

    puts("|-----------------------------------------|");
    printf("| 原始视频 time_base-> num : %d \n", video_time_base.num);
    printf("| 原始视频 time_base-> den : %d \n", video_time_base.den);
    puts("|-----------------------------------------|\n");

    ret = avformat_find_stream_info(input_av_format_context, NULL);
    if (ret < 0) {
        printf("avformat find stream info failed: %d \n", ret);
        return ret;
    }

    avctx = avcodec_alloc_context3(NULL);
    ret   = avcodec_parameters_to_context(avctx, input_av_format_context->streams[0]->codecpar);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }
    AVCodec *codec = avcodec_find_decoder(avctx->codec_id);
    //  Initialize the AVCodecContext
    if ((ret = avcodec_open2(avctx, codec, NULL)) < 0) {
        printf("open open codec failed: %d \n", ret);
        return ret;
    }

    pkt   = av_packet_alloc();
    frame = av_frame_alloc();
    return ret;
}


int init_avio_context() {
    puts("2:  avio context.");
    avInputFormat = av_find_input_format("mp4");;

    avio_ctx_buffer = av_malloc(AVIO_BUFFER_SIZE);
    printf("    avio_ctx_buffer address is : %p \n", avio_ctx_buffer);

    if (!avio_ctx_buffer) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }

    // 核心代码，也是难点；
    avio_ctx = avio_alloc_context(avio_ctx_buffer, AVIO_BUFFER_SIZE,
                                  0, &bd, &read_packet, NULL, &seek_in_buffer);
    if (!avio_ctx) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    return 0;

}


int init_output_ffmpeg() {
    int result = -1;
    puts("初始化 输出部分相关参数；");
    result = avformat_alloc_output_context2(&fmt_ctx_out, NULL, NULL, filename_out);

    if (!fmt_ctx_out) {
        printf("error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }
    //添加一路流到容器上下文
    st = avformat_new_stream(fmt_ctx_out, NULL);
    // 设置时间基
    st->time_base = input_av_format_context->streams[0]->time_base;
    pkt_out = av_packet_alloc();
    return result;
}

/**
 * 开始接收数据包
 * @return
 */
int start_receive_packet() {
    int ret = -1;
    for (;;) {
        ret = avcodec_receive_packet(enc_ctx, pkt_out);
        //这里不可能返回 EAGAIN，如果有直接退出。
        if (ret == AVERROR(EAGAIN)) {
            printf("avcodec receive packet error code %d \n", ret);
            return ret;
        }

        if (AVERROR_EOF == ret) {
            break;
        }

        ret = start_write_packet();
        if (ret < 0) {
            printf("start write packet failed %d \n", ret);
            return ret;
        }
        av_packet_unref(pkt_out);
    }
    return ret;
}

int start_write_packet() {
    // 设置 AVPacket 的 stream_index ，这样才知道是哪个流的。
    pkt_out->stream_index = st->index;
    // 转换 AVPacket 的时间基为 输出流的时间基。
    pkt_out->pts          = av_rescale_q_rnd(pkt_out->pts, input_av_format_context->streams[0]->time_base,
                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt_out->dts          = av_rescale_q_rnd(pkt_out->dts, input_av_format_context->streams[0]->time_base,
                                             st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

    pkt_out->duration = av_rescale_q_rnd(pkt_out->duration, input_av_format_context->streams[0]->time_base,
                                         st->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

    print_avpacket_info(pkt);

    return av_interleaved_write_frame(fmt_ctx_out, pkt_out);
}

void print_avpacket_info(AVPacket *temp_packet) {
    puts("↓------------------------------------------↓");
    printf("| pkt_out->pts      is : %lld\n", temp_packet->pts);
    printf("| pkt_out->dts      is : %lld\n", temp_packet->dts);
    printf("| pkt_out->duration is : %lld\n", temp_packet->duration);
    puts("↑------------------------------------------↑\n");

}


int init_encode_context() {
    int ret = -1;
    if (enc_ctx != NULL) {
        return ret;
    }
    puts("初始 编码器相关参数:");
    puts("初始化编码器.");
    //打开编码器，并且设置 编码信息。
    AVCodec *encode = avcodec_find_encoder(AV_CODEC_ID_H264);
    enc_ctx = avcodec_alloc_context3(encode);
    enc_ctx->codec_type             = AVMEDIA_TYPE_VIDEO;
    enc_ctx->bit_rate               = 400000;
    enc_ctx->framerate              = avctx->framerate;
    enc_ctx->gop_size               = 30;
    enc_ctx->max_b_frames           = 10;
    enc_ctx->profile                = FF_PROFILE_H264_MAIN;
    /*
     * 其实下面这些信息在容器那里也有，也可以一开始直接在容器那里打开编码器
     * 我从 AVFrame 里拿这些编码器参数是因为，容器的不一样就是最终的。
     * 因为你解码出来的 AVFrame 可能会经过 filter 滤镜，经过滤镜之后信息就会变换，但是本文没有使用滤镜。
     */
    //编码器的时间基要取 AVFrame 的时间基，因为 AVFrame 是输入。AVFrame 的时间基就是 流的时间基。
    enc_ctx->time_base              = input_av_format_context->streams[0]->time_base;
    enc_ctx->width                  = input_av_format_context->streams[0]->codecpar->width;
    enc_ctx->height                 = input_av_format_context->streams[0]->codecpar->height;
    enc_ctx->sample_aspect_ratio    = st->sample_aspect_ratio = frame->sample_aspect_ratio;
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

    /* 现在我们需要把 编码器参数复制给流，解码的时候是 从流赋值参数给解码器。
     * 现在要反着来。
     * */
    ret = avcodec_parameters_from_context(st->codecpar, enc_ctx);
    if (ret < 0) {
        printf("error code %d \n", ret);
        return ret;
    }
    if ((ret = avcodec_open2(enc_ctx, encode, NULL)) < 0) {
        printf("open codec faile %d \n", ret);
        return ret;
    }
    //正式打开输出文件
    if ((ret = avio_open2(&fmt_ctx_out->pb, filename_out, AVIO_FLAG_WRITE,
                          &fmt_ctx_out->interrupt_callback, NULL)) < 0) {
        printf("avio_open2 fail %d \n", ret);
        return ret;
    }
    //要先写入文件头部。
    ret      = avformat_write_header(fmt_ctx_out, NULL);
    if (ret < 0) {
        printf("avformat_write_header fail %d \n", ret);
        return ret;
    }

    return ret;
}

void free_all() {
    // av_free(&avio_ctx_buffer);
    avio_context_free(&avio_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_packet_free(&pkt_out);

    //关闭编码器，解码器。
    avcodec_close(avctx);
    avcodec_close(enc_ctx);

    //释放容器内存。
    avformat_free_context(input_av_format_context);
    avformat_free_context(fmt_ctx_out);
}


int main() {
    int ret = 0;
    start_read_file();
    init_avio_context();
    init_input_ffmpeg();

    init_output_ffmpeg();
    int read_end = 0;

    // level 1
    for (;;) {
        if (1 == read_end) {
            break;
        }
        ret = av_read_frame(input_av_format_context, pkt);
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
                printf("read error code %d, %s \n", ret, av_err2str(ret));
                return ENOMEM;
            }

            TAG_RETRY:
            if (avcodec_send_packet(avctx, pkt) == AVERROR(EAGAIN)) {
                printf("Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                //这里可以考虑休眠 0.1 秒，返回 EAGAIN 通常是 ffmpeg 的内部 api 有bug
                goto TAG_RETRY;
            }
            //释放 pkt 里面的编码数据
            av_packet_unref(pkt);
        }
        puts("↓------------------------------------------↓");
        printf("| one AVPacket `s duration is : %lld\n", pkt->duration);
        puts("↑------------------------------------------↑\n");

        // receive the frame;
        //循环不断从解码器读数据，直到没有数据可读。
        // level 2
        for (;;) {
            // 读取 AVFrame
            ret = avcodec_receive_frame(avctx, frame);
            // case 1:
            if (AVERROR(EAGAIN) == ret) {
                break;
            }

            puts("↓********************************************↓");
            printf("| one AVFrame `s pkt_duration is : %lld\n", frame->pkt_duration);
            puts("↑*******************************************↑\n");

            // case 2:
            if (AVERROR_EOF == ret) {
                avcodec_send_frame(enc_ctx, NULL);
                start_receive_packet();
                av_write_trailer(fmt_ctx_out);
                // 跳出 第二层 for，文件已经解码完毕。
                puts("跳出 第二层 for，文件已经解码完毕。");
                read_end = 1;
                break;
            }

            if (ret >= 0) {
                //只有解码出来一个帧，才可以开始初始化编码器。
                init_encode_context();
                //往编码器发送 AVFrame，然后不断读取 AVPacket
                ret = avcodec_send_frame(enc_ctx, frame);
                if (ret < 0) {
                    printf("avcodec_send_frame fail %d \n", ret);
                    return ret;
                }

                // level 3
                for (;;) {
                    ret = avcodec_receive_packet(enc_ctx, pkt_out);
                    if (ret == AVERROR(EAGAIN)) {
                        break;
                    }

                    if (ret < 0) {
                        printf("avcodec receive packet fail %d \n", ret);
                        return ret;
                    }

                    ret = start_write_packet();

                    if (ret < 0) {
                        printf("start write packet failed %d \n", ret);
                        return ret;
                    }
                    av_packet_unref(pkt_out);
                }
                continue;
            }

            printf("other fail \n");
            return ret;
        }
    }

    free_all();
    puts("------------done--------\n");
    return 0;
}


