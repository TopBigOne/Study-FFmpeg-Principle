#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"

}

using namespace std;

void convert_video(const char *inputFile, const char *outFileName);

void convert_video2(const char *inputFile, const char *outFileName);

int convert_video3(const char *input_filename, const char *output_filename);

int convert_video4(const char *input_filename, const char *output_filename);

int convert_video2_400_300(const char *input_filename, const char *output_filename);


#define PRINT_STEP_ERROR(stepError) \
    do {\
        cerr << (stepError) << endl;\
        return;\
    } while (0);\


void convert_video(const char *inputFile, const char *outFileName) {
    AVFormatContext *avFormatContext = avformat_alloc_context();
    int             result           = avformat_open_input(&avFormatContext, inputFile, nullptr, nullptr);
    if (result != 0) {

        PRINT_STEP_ERROR("step 1")
    }
    result = avformat_find_stream_info(avFormatContext, nullptr);
    if (result != 0) {

        PRINT_STEP_ERROR("step 2")
    }

    int      videoStreamIndex = -1;
    for (int i                = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }
    if (videoStreamIndex == -1) {
        return;
    }
    AVCodecParameters *codecParameters = avFormatContext->streams[videoStreamIndex]->codecpar;
    AVCodec           *inputCodec      = avcodec_find_decoder(codecParameters->codec_id);
    if (inputCodec == nullptr) {
        PRINT_STEP_ERROR("step 3")
    }

    AVCodecContext *inputCodecCtx = avcodec_alloc_context3(inputCodec);
    if (inputCodecCtx == nullptr) {
        PRINT_STEP_ERROR("step 4")
    }
    // 用于将AVCodecParameters结构体中的参数值复制到AVCodecContext结构体中。
    result = avcodec_parameters_to_context(inputCodecCtx, codecParameters);
    if (result != 0) {
        PRINT_STEP_ERROR("step 5")
    }
    result = avcodec_open2(inputCodecCtx, inputCodec, nullptr);
    if (result != 0) {
        PRINT_STEP_ERROR("step 5")

    }
    // -----------------------------------------处理输出-------------------------------------------------------
    AVFormatContext *outputFormatCtx = nullptr;
    result = avformat_alloc_output_context2(&outputFormatCtx, nullptr, nullptr, outFileName);
    if (result != 0) {
        PRINT_STEP_ERROR("step 6")

    }
    AVStream *outputStream = avformat_new_stream(outputFormatCtx, nullptr);
    if (outputStream == nullptr) {
        PRINT_STEP_ERROR("step 7")

    }
    AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (output_codec == nullptr) {
        PRINT_STEP_ERROR("step 8")
    }

    AVCodecContext *outputCodecCtx = avcodec_alloc_context3(output_codec);

    outputCodecCtx->height    = 1080;
    outputCodecCtx->width     = 1920;
    outputCodecCtx->pix_fmt   = AV_PIX_FMT_YUV420P;
    outputCodecCtx->time_base = {1, 30};

    result = avcodec_open2(outputCodecCtx, output_codec, nullptr);
    if (result != 0) {
        PRINT_STEP_ERROR("step 8")
    }
    result = avcodec_parameters_from_context(outputStream->codecpar, outputCodecCtx);
    if (result != 0) {
        PRINT_STEP_ERROR("step 9")
    }

    if (!(outputFormatCtx->oformat->flags & AVFMT_NOFILE)) {
        result = avio_open(&outputFormatCtx->pb, outFileName, AVIO_FLAG_WRITE);
        if (result < 0) {
            PRINT_STEP_ERROR("step 10")
        }
    }

    result = avformat_write_header(outputFormatCtx, nullptr);
    if (result < 0) {
        PRINT_STEP_ERROR("step 11")
    }

    AVFrame *frame      = av_frame_alloc();
    AVFrame *scaleFrame = av_frame_alloc();

    if (frame == nullptr || scaleFrame == nullptr) {
        PRINT_STEP_ERROR("step 12")
    }

    scaleFrame->format = outputCodecCtx->pix_fmt;
    scaleFrame->width  = outputCodecCtx->width;
    scaleFrame->height = outputCodecCtx->height;
    // 申请内存
    result = av_frame_get_buffer(scaleFrame, 0);
    if (result < 0) {
        PRINT_STEP_ERROR("step 12")
    }

    SwsContext *sws_ctx = sws_getContext(inputCodecCtx->width, inputCodecCtx->height, inputCodecCtx->pix_fmt,
                                         outputCodecCtx->width, outputCodecCtx->height,
                                         outputCodecCtx->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);


    if (sws_ctx == nullptr) {
        PRINT_STEP_ERROR("step 12")
    }

    AVPacket *packet = av_packet_alloc();
    // 开始往 avpacket 包中写入数据
    while (av_read_frame(avFormatContext, packet) >= 0) {
        // case 1:
        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }
        // case 2:
        if (avcodec_send_packet(inputCodecCtx, packet) < 0) {
            break;
        }
        // case 3:
        while (avcodec_receive_frame(inputCodecCtx, frame) >= 0) {
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, inputCodecCtx->height, scaleFrame->data,
                      scaleFrame->linesize);
            scaleFrame->pts = av_rescale_q(frame->pts, inputCodecCtx->time_base, outputCodecCtx->time_base);

            // 转换了以后，立马开始处理输出
            if (avcodec_send_frame(outputCodecCtx, scaleFrame) < 0) {
                break;
            }
            AVPacket *outputPacket = av_packet_alloc();
            while (avcodec_receive_packet(outputCodecCtx, outputPacket) >= 0) {
                outputPacket->stream_index = outputStream->index;
                // 用于将 AVPacket 中各种时间值从一种时间基转换为另一种时间基。
                av_packet_rescale_ts(outputPacket, outputCodecCtx->time_base, outputStream->time_base);
                av_interleaved_write_frame(outputFormatCtx, outputPacket);
                av_packet_unref(outputPacket);
            }
        }
        av_packet_unref(packet);


    }
    av_write_trailer(outputFormatCtx);

    sws_freeContext(sws_ctx);


}


int main() {


    auto *input = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/31_4kTo1080p/doc/raw_mono.mp4";

    auto *output  = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/31_4kTo1080p/doc/result_mono.mp4";
    auto *output2 = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/31_4kTo1080p/doc/result_mono_2.mp4";
    auto *output3 = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/31_4kTo1080p/doc/result_mono_3.mp4";
    auto *output4 = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/31_4kTo1080p/doc/result_mono_4.mp4";
    auto *output5 = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/31_4kTo1080p/doc/result_mono_5.mp4";

    //convert_video4(input, output4);
    //convert_video2_400_300(input, output5);

    // convert_video2(input, output2);
      convert_video(input, output);
    return 0;
}
