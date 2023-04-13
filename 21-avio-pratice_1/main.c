#include <stdio.h>

#include <libavformat/avformat.h>

#define MIN(a, b) (a<b)?a:b
#define SIZE_4KB 4096
char filename_out[]      = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/21-avio-pratice_1/out/juren-30s-5.mp4";
char origin_video_path[] = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/21-avio-pratice_1/juren-30s.mp4";


AVFormatContext *input_av_format_context = NULL;
AVPacket        *avPacket                = NULL;
AVFrame         *avFrame                 = NULL;

AVFormatContext *fmt_out_ctx    = NULL;
AVStream        *out_stream     = NULL;
AVPacket        *pkt_out        = NULL;
AVCodecContext  *decode_context = NULL;
AVCodec         *decode         = NULL;

AVIOContext   *avio_ctx        = NULL;
AVInputFormat *avInputFormat   = NULL;
uint8_t       *avio_ctx_buffer = NULL;


// 编码器上下文和编码器
AVCodecContext *encode_ctx = NULL;


typedef struct _BufferData {
    uint8_t *ptr;       // 指向buffer数据中，还没被io上下文消耗的位置
    uint8_t *ori_ptr;   // 也是指向buffer数据的指针，之所以 定义ori_ptr,是用在自定义seek函数中；
    size_t  size;
    size_t  file_size;
}              BufferData;

BufferData buffer_data = {0};


int init_input_ffmpeg();

int start_read_video_file();

int init_encode_avpacket_context();


uint8_t *readFile(char *path, size_t *length) {
    FILE *pfile;

    pfile = fopen(path, "rb");

    uint8_t *data;

    if (pfile == NULL) {
        return NULL;
    }

    fseek(pfile, 0, SEEK_END);

    *length = ftell(pfile);

    data = (uint8_t *) malloc((*length) * sizeof(uint8_t));
    rewind(pfile);
    //   uint8_t VS int
    *length = fread(data, sizeof(uint8_t), *length, pfile);
    fclose(pfile);
    return data;
}

void start_read_file() {
    puts("1: 开始读取本地的mp4文件");
    uint8_t *input;
    size_t  file_len;
    // start ....
    input = readFile(origin_video_path, &file_len);
    puts("  为 BufferData 的属性赋值");
    buffer_data.ptr       = input;
    buffer_data.ori_ptr   = input;
    buffer_data.size      = file_len;
    buffer_data.file_size = file_len;
}

static int read_packet(void *opaque, uint8_t *des_buffer, int buffer_size) {
    puts("     read_packet ");
    printf("    ***** des_buffer pointer address is : %p \n", des_buffer);
    BufferData *bufferData = (BufferData *) (opaque);
    buffer_size = MIN(((int) bufferData->size), buffer_size);
    if (!buffer_size) {
        printf("no buf_size pass to read_packet,%d,%zu\n", buffer_size, bufferData->size);
        return EAGAIN;
    }
    // 内存copy，注意，别传错了。。。
    memcpy(des_buffer, bufferData->ptr, buffer_size);

    bufferData->ptr += buffer_size;
    bufferData->size -= buffer_size;
    return buffer_size;
}

static int64_t seek_in_buffer(void *opaque, int64_t offset, int whence) {
    BufferData *temp_buffer_data = (BufferData *) opaque;
    int64_t    ret               = -1;
    printf("    seek_in_buffer : whence=%d , offset=%lld , file_size=%zu\n", whence, offset,
           temp_buffer_data->file_size);
    switch (whence) {
        case AVSEEK_SIZE:
            ret = (int64_t) temp_buffer_data->file_size;
            break;
        case SEEK_SET:
            temp_buffer_data->ptr  = temp_buffer_data->ori_ptr + offset;
            temp_buffer_data->size = temp_buffer_data->file_size - offset;
            ret = (int64_t) temp_buffer_data->ptr;
            break;

    }
    return ret;
}

int init_avio_context() {
    puts("2: init_avio_context");
    avInputFormat   = av_find_input_format("mp4");
    avio_ctx_buffer = av_malloc(SIZE_4KB);

    if (avio_ctx_buffer == NULL) {
        perror("av_malloc in error.");
        return ENOMEM;
    }
    printf("    avio_ctx_buffer address is : %p \n", avio_ctx_buffer);

    avio_ctx = avio_alloc_context(avio_ctx_buffer, SIZE_4KB,
                                  0,
                                  &buffer_data, &read_packet, NULL, &seek_in_buffer);

    if (avio_ctx == NULL) {
        perror(" avio_ctx is still NULL.");
        return -1;
    }
    return 0;
}

int init_input_ffmpeg() {
    puts("3: init_input_ffmpeg");
    int result;
    input_av_format_context = avformat_alloc_context();
    if (input_av_format_context == NULL) {
        printf("avformat_alloc_context : error code %d \n", AVERROR(ENOMEM));
        return ENOMEM;
    }

    input_av_format_context->pb = avio_ctx;

    result = avformat_open_input(&input_av_format_context, NULL, avInputFormat, NULL);
    if (result < 0) {
        fprintf(stderr, "avformat_open_input in ERROR: %s", av_err2str(result));
        return result;
    }

    result = avformat_find_stream_info(input_av_format_context, NULL);
    if (result < 0) {
        fprintf(stderr, "avformat_find_stream_info in ERROR: %s", av_err2str(result));
        return result;
    }

    decode_context = avcodec_alloc_context3(NULL);
    result         = avcodec_parameters_to_context(decode_context, input_av_format_context->streams[0]->codecpar);
    if (result < 0) {
        fprintf(stderr, "avcodec_parameters_to_context in ERROR: %s", av_err2str(result));
        return result;
    }

    decode = avcodec_find_decoder(decode_context->codec_id);

    result = avcodec_open2(decode_context, decode, NULL);
    if (result < 0) {
        fprintf(stderr, " decode : avcodec_open2 in ERROR: %s", av_err2str(result));
        return result;
    }

    avPacket = av_packet_alloc();
    avFrame  = av_frame_alloc();
    return result;
}


int init_output_ffmpeg() {
    puts("4: init_output_ffmpeg");
    int result;
    result = avformat_alloc_output_context2(&fmt_out_ctx, NULL, NULL, filename_out);
    if (result < 0) {
        return result;
    }

    out_stream = avformat_new_stream(fmt_out_ctx, NULL);
    // set stream time base.
    out_stream->time_base = input_av_format_context->streams[0]->time_base;
    pkt_out = av_packet_alloc();
    return result;
}


int start_write_packet() {
    puts("  start_write_packet");
    pkt_out->stream_index                = out_stream->index;
    AVRational      origin_time_base     = input_av_format_context->streams[0]->time_base;
    AVRational      out_stream_time_base = out_stream->time_base;
    enum AVRounding packet_rounding      = AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX;

    pkt_out->pts      = av_rescale_q_rnd(pkt_out->pts, origin_time_base, out_stream_time_base, packet_rounding);
    pkt_out->dts      = av_rescale_q_rnd(pkt_out->dts, origin_time_base, out_stream_time_base, packet_rounding);
    pkt_out->duration = av_rescale_q_rnd(pkt_out->duration, origin_time_base, out_stream_time_base, packet_rounding);

    return av_interleaved_write_frame(fmt_out_ctx, pkt_out);
}


/**
 * 编码剩余的frame
 * @return
 */
int start_encode_left_packet() {
    int result;
    for (;;) {
        result = avcodec_receive_packet(encode_ctx, pkt_out);
        if (result == AVERROR(EAGAIN)) {
            return result;
        }
        if (result == AVERROR_EOF) {
            // need more frame
            break;
        }
        result = start_write_packet();
        if (result < 0) {
            return result;
        }
        av_packet_unref(pkt_out);
    }

    return result;
}


int init_encode_avpacket_context() {
    int result = -1;
    if (encode_ctx != NULL) {
        return -1;
    }
    puts("6：init_encode_avpacket_context");
    AVCodec *encode = avcodec_find_encoder(AV_CODEC_ID_H264);
    encode_ctx = avcodec_alloc_context3(encode);
    // 设置编码器相关参数相关参数
    encode_ctx->codec_type   = AVMEDIA_TYPE_VIDEO;
    encode_ctx->bit_rate     = 500000;
    encode_ctx->framerate    = decode_context->framerate;
    encode_ctx->gop_size     = 30;
    encode_ctx->max_b_frames = 10;
    encode_ctx->profile      = FF_PROFILE_H264_MAIN;

    encode_ctx->time_base              = input_av_format_context->streams[0]->time_base;
    encode_ctx->height                 = input_av_format_context->streams[0]->codecpar->height;
    encode_ctx->width                  = input_av_format_context->streams[0]->codecpar->width;
    encode_ctx->sample_aspect_ratio    = out_stream->sample_aspect_ratio = avFrame->sample_aspect_ratio;
    encode_ctx->pix_fmt                = avFrame->format;
    encode_ctx->color_range            = avFrame->color_range;
    encode_ctx->color_trc              = avFrame->color_trc;
    encode_ctx->color_primaries        = avFrame->color_primaries;
    encode_ctx->colorspace             = avFrame->colorspace;
    encode_ctx->chroma_sample_location = avFrame->chroma_location;


    encode_ctx->field_order = AV_FIELD_PROGRESSIVE;
    // 将编码器的参数，赋值给输出流
    result = avcodec_parameters_from_context(out_stream->codecpar, encode_ctx);
    if (result < 0) {
        return result;
    }
    result = avcodec_open2(encode_ctx, encode, NULL);
    if (result < 0) {
        return result;
    }
    // core , open the output file
    result = avio_open2(&fmt_out_ctx->pb, filename_out, AVIO_FLAG_WRITE,
                        &fmt_out_ctx->interrupt_callback, NULL);
    if (result < 0) {
        return result;
    }
    puts("   write out file header");
    // write header
    result = avformat_write_header(fmt_out_ctx, NULL);
    if (result < 0) {
        return result;
    }
    return result;
}


void start_decode() {
    puts("5: start_decode");
    int result   = -1;
    int read_end = 0;
    for (;;) {
        if (read_end == 1) {
            break;
        }
        result = av_read_frame(input_av_format_context, avPacket);
        if (avPacket->stream_index == 1) {
            av_packet_unref(avPacket);
            continue;
        }
        if (result == AVERROR_EOF) {
            avcodec_send_packet(decode_context, NULL);
        } else {
            if (result != 0) {
                return;
            }
            TAG_RETRY:
            if ((avcodec_send_packet(decode_context, avPacket)) == AVERROR(EAGAIN)) {
                goto TAG_RETRY;
            }
            av_packet_unref(avPacket);
        }


        // start get frame via packet
        for (;;) {
            result = avcodec_receive_frame(decode_context, avFrame);
            // case 1:
            if (result == AVERROR_EOF) {
                read_end = 1;
                avcodec_send_frame(encode_ctx, NULL);
                start_encode_left_packet();
                puts("packet 封装完毕，开始写trailer.");
                av_write_trailer(fmt_out_ctx);
                break;
            }
            // case 2：
            if (result == AVERROR(EAGAIN)) {
                break;
            }
            if (result >= 0) {
                init_encode_avpacket_context();
                result = avcodec_send_frame(encode_ctx, avFrame);
                if (result < 0) {
                    return;
                }
                puts("start_encode_packet");

                for (;;) {
                    result = avcodec_receive_packet(encode_ctx, pkt_out);
                    if (result == AVERROR(EAGAIN)) {
                        break;
                    }
                    if (result < 0) {
                        return ;
                    }

                    result = start_write_packet();
                    if (result < 0) {
                        return ;
                    }
                    av_packet_unref(pkt_out);

                }

                continue;
            }
            perror("未知错误");
        }

    }

}

int main() {
    //  start_read_video_file();

    start_read_file();
    init_avio_context();
    init_input_ffmpeg();

    init_output_ffmpeg();
    start_decode();

    return 0;
}
