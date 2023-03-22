#include <stdio.h>

#include "libavformat/avformat.h"

#include <libavutil/imgutils.h>

#define  video_path  "/Users/dev/Desktop/mp4/貳佰《玫瑰》1080P.mp4"

void printAVFrameInfo(AVFrame *pFrame);

AVRational avRational;

int main() {
    AVFormatContext *avFormatContext = NULL;

    int type = 1;
    int ret = 0;
    int err;
    // step 1:
    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        printf("error code %d", AVERROR(ENOMEM));
        return ENOMEM;
    }
    // step 2:
    err = avformat_open_input(&avFormatContext, video_path, NULL, NULL);
    int64_t all_duration = avFormatContext->duration;
    fprintf(stderr, "all_duration : %lld \n", all_duration);


    if (err < 0) {
        fprintf(stderr, " can not open the file,%d", err);
        return err;
    }
    if (type == 1) {
        // step 3:
        AVCodecContext *avCodecContext = avcodec_alloc_context3(NULL);


        // step 4:
        ret = avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
        if (ret < 0) {
            fprintf(stderr, "error code %d\n", ret);
            return ret;
        }

        avRational = avFormatContext->streams[0]->time_base;
        AVCodec *avCodec = avcodec_find_decoder(avCodecContext->codec_id);
        ret = avcodec_open2(avCodecContext, avCodec, NULL);
        if (ret < 0) {
            printf("open codec failed %d \n", ret);
            return ret;

        }

        AVPacket *avPacket = av_packet_alloc();
        AVFrame *avFrame = av_frame_alloc();
        int frame_num = 0;
        int read_end = 0;
        for (;;) {
            if (read_end == 1) {
                printf("read end.\n");
                break;
            }

            ret = av_read_frame(avFormatContext, avPacket);

            // 跳过，不处理音频包
            if (avPacket->stream_index == 1) {
                // printf("skip the audio packet.\n");
                continue;
            }

            // case 1:
            if (ret == AVERROR_EOF) {
                printf("CASE: 1\n");
                printf("--->read the AVPacket over.<---\n");
                // 已经读完文件，
                // 读完文件，这时候，avpacket 的 data 和size 应该是null；
                // 冲刷解码器
                avcodec_send_packet(avCodecContext, avPacket);
                // 释放 avPacket 里的编码数据
                av_packet_unref(avPacket);
                // step 2： 循环不断从解码器中读取数据，直到没有数据可读
                for (;;) {
                    printf("    Start receive the frame from the AVCodecContext by LOOP.\n");
                    // 读取AVFrame;
                    // todo : 是不是给 指针avFame 直接赋值了啊？ debug 一下?
                    ret = avcodec_receive_frame(avCodecContext, avFrame);
                    /*
                     * 释放framne里的yuv数据，
                     * 由于avcodec_receive_frame() 函数会调用 av_frame_unref(),所以下面的代码可以注释
                     * 所以，我们不需要手动unref 这个 AVFrame;
                     *
                     * */
                    // av_frame_unref(avFrame);
                    if (ret == AVERROR(EAGAIN)) {
                        printf("    There are many AVPacket.\n");
                        // EAGAIN 代表 需要更多的 AVPacket
                        // 跳出 第一层for ,让解码器 拿到更多的 AVPacket
                        break;
                    } else if (ret == AVERROR_EOF) {
                        printf("    Decode the frame over.\n");
                        // AVERROR_EOF : 代码之前已经往解码器中 发送了一个data 和 size 都是 NULL 的AVPacket
                        // 发送NULL 的AVPacket 是 提示解码器把 所有的缓存帧 全都刷出来；
                        // 通常 只有在 读完输入文件才会发送NULL 的AVPacket ，或者需要 解码另一个的视频流，才会这么干；

                        // 跳出第二层循环，文件解码已经完毕
                        read_end = 1;
                        break;
                    } else if (ret >= 0) {
                        printAVFrameInfo(avFrame);

                    } else {
                        fprintf(stderr, "Other fail\n");
                        return ret;
                    }

                }

            }

            // case 2: ret =0;
            if (ret == 0) {
                printf("CASE: 2\n");
                retry:
                // send
                if (avcodec_send_packet(avCodecContext, avPacket) == AVERROR(EAGAIN)) {
                    printf("    Receive frame and send packet both returned EAGAIN, which is an API violation.\n");
                    // 这里可以考虑休眠0.1 秒，返回EAGAIN 通常是ffmpeg 的内部api 有bug
                    goto retry;
                } else {
                    // 释放AVPacket里的编码数据
                    av_packet_unref(avPacket);
                    // 循环不断从解码器读数据，直到没有数据可读
                    for (;;) {
                        // 读取 AVFrame
                        ret = avcodec_receive_frame(avCodecContext, avFrame);
                        if (ret == AVERROR(EAGAIN)) {
                            break;
                        } else if (ret == AVERROR_EOF) {
                            read_end = 1;
                            break;
                        } else if (ret >= 0) {
                            printAVFrameInfo(avFrame);
                        } else {
                            fprintf(stderr, "other failed \n");
                        }
                    }
                }

            }

        }
        av_frame_free(&avFrame);
        av_packet_free(&avPacket);
        avcodec_close(avCodecContext);

    }

    return ret;
}

void printAVFrameInfo(AVFrame *avFrame) {
    // 解码出一帧的YUV数据，打印一些信息
    printf("-----Decode success -----\n");
    int width = avFrame->width;
    int height = avFrame->height;
    int64_t pts = avFrame->pts;

    int64_t duration = avFrame->pkt_duration;
    int format = avFrame->format;
    int key_frame = avFrame->key_frame;
    enum AVPictureType avPicture = avFrame->pict_type;
    int num = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 1920, 1080, 1);
    int64_t pkt_pos = avFrame->pkt_pos;

    double my_time = (double) pts * av_q2d(avRational);
    printf("|-----------------------------------------↓\n");
    printf("| width        :%d \n ", width);
    printf("| height       :%d \n ", height);
    printf("| pts          :%lld \n ", pts);
    printf("| my_time      :%f \n ", my_time);
    printf("| duration     :%lld \n ", duration);
    printf("| format       :%d \n ", format);
    printf("| key_frame    :%d \n ", key_frame);
    printf("| avPicture    :%d \n ", avPicture);
    printf("| num          :%d \n ", num);
    printf("| num          :%d \n ", num);
    printf("| pkt_pos      :%lld \n ", pkt_pos);
    printf("|-----------------------------------------↑\n");


}
