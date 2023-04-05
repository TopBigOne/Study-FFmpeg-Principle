#include <stdio.h>

#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/bprint.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>

int save_rgb_to_file(uint8_t *pixels[4], int pitch[4], int height, int num);


void my_code();

int main() {
    my_code();

    return 0;
}

void my_code() {
    int               err              = 0;
    int               result           = 0;
    int               read_end         = 0;
    int               frame_num        = 0;
    char              *file_name       = "/Users/dev/Desktop/mp4/雨中的戀人們.mp4";
    struct SwsContext *img_convert_ctx = NULL;

    AVFormatContext *avFormatContext = avformat_alloc_context();
    result = avformat_open_input(&avFormatContext, file_name, NULL, NULL);
    AVCodecContext *avCodecContext = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);

    AVCodec *avCodec = avcodec_find_decoder(avCodecContext->codec_id);
    result = avcodec_open2(avCodecContext, avCodec, NULL);


    AVPacket *avPacket     = av_packet_alloc();
    AVFrame  *avFrame      = av_frame_alloc();


    int sws_flags     = SWS_BICUBIC;
    int result_format = AV_PIX_FMT_BGRA;
    int result_width  = 300;
    int result_height = 300;
    // 确定内存大小
    int buf_size      = av_image_get_buffer_size(result_format, result_width, result_height, 1);

    for (;;) {
        if (read_end == 1) {
            break;
        }
        result = av_read_frame(avFormatContext, avPacket);
        if (avPacket->stream_index == 1) {
            av_packet_unref(avPacket);
            continue;
        }
        if (result == AVERROR_EOF) {
            avcodec_send_packet(avCodecContext, NULL);
        } else {
            if (result != 0) {
                return;
            }
            TAG_RETRY:
            if ((avcodec_send_packet(avCodecContext, avPacket)) == AVERROR(EAGAIN)) {
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
                if (img_convert_ctx == NULL) {
                    img_convert_ctx = sws_getCachedContext(img_convert_ctx,
                                                           avFrame->width,
                                                           avFrame->height,
                                                           avFrame->format,
                                                           result_width,
                                                           result_height,
                                                           result_format,
                                                           sws_flags,// 算法
                                                           NULL, NULL, NULL);
                    if (img_convert_ctx == NULL) {
                        av_log(NULL, AV_LOG_FATAL, "no memory 1\n");
                        return;
                    }

                    // 申请内存
                    uint8_t *buffer = (uint8_t *) av_malloc(buf_size);
                    if (!buffer) {
                        av_log(NULL, AV_LOG_FATAL, "no memory. 2\n");
                    }
                    uint8_t *pixels[4];
                    int     pitch[4];
                    // 把内存映射到数组里面
                    av_image_fill_arrays(pixels, pitch, buffer, result_format,
                                         result_width, result_height, 1);

                    sws_scale(img_convert_ctx, (const uint8_t *const *) avFrame->data, avFrame->linesize, 0,
                              avFrame->height,
                              pixels,
                              pitch);

                    save_rgb_to_file(pixels, pitch, result_height, frame_num);
                    av_frame_unref(avFrame);
                    av_freep(&buffer);
                    frame_num++;
                    if (frame_num > 10) {
                        break;
                    }
                    continue;


                }

            }
        }
    }
    av_frame_free(&avFrame);
    av_packet_free(&avPacket);

    sws_freeContext(img_convert_ctx);
    img_convert_ctx = NULL;

    //关闭编码器，解码器。
    avcodec_close(avCodecContext);

    //释放容器内存。
    avformat_free_context(avFormatContext);
    printf("my code done \n");


}

int save_rgb_to_file(uint8_t *pixels[4], int temp_pitch[4], int height, int num) {
    //拼接文件名
    char pic_name[200] = {0};
    sprintf(pic_name, "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/14_sws_scale-2/doc/rgba_8888_%d.yuv", num);

    //写入文件
    FILE *fp = NULL;
    fp = fopen(pic_name, "wb+");
    fwrite(pixels[0], 1, temp_pitch[0] * height, fp);
    fclose(fp);
    printf("save success \n");
    return 0;
}
