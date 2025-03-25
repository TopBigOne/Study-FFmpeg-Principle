//
// Created by dev on 2024/6/24.
//

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <iostream>


/*
 * GWt /chat HTTP/1.t
 * Host: www.baidu.com
 * Upgrade:websocket
 * connection: Upgrade
 * sec-socket-key:dsfdsf==
 * sec-socket-version:13
 * Origin : www.tencent.com
 * sec-socket-Protocol:chat ,superchat
 * sec-socket-Extension:sfdsr
 *
 *
 * */

/**
 * websocket 的body
 */
typedef struct ws_body {
    uint8_t  fin: 1;
    uint8_t  rsv1: 1;
    uint8_t  rsv2: 1;
    uint8_t  rsv3: 1;
    uint8_t  opcode: 4; // 0x08:close,0x09:ping ;0xa:pong
    uint8_t  mask: 1;
    uint8_t  pay_load_length: 7;
    uint32_t masing_key;
    uint8_t  *pay_load_data;// core data;
};

void convert_video2(const char *input_filename, const char *output_filename) {
    av_register_all();

    AVFormatContext *input_format_ctx = nullptr;
    if (avformat_open_input(&input_format_ctx, input_filename, nullptr, nullptr) != 0) {
        std::cerr << "Could not open input file" << std::endl;
        return;
    }

    if (avformat_find_stream_info(input_format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        avformat_close_input(&input_format_ctx);
        return;
    }

    int           video_stream_index = -1;
    for (unsigned i                  = 0; i < input_format_ctx->nb_streams; i++) {
        if (input_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVCodecParameters *codecpar    = input_format_ctx->streams[video_stream_index]->codecpar;
    AVCodec           *input_codec = avcodec_find_decoder(codecpar->codec_id);
    if (!input_codec) {
        std::cerr << "Could not find codec" << std::endl;
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVCodecContext *input_codec_ctx = avcodec_alloc_context3(input_codec);
    if (!input_codec_ctx) {
        std::cerr << "Could not allocate codec context" << std::endl;
        avformat_close_input(&input_format_ctx);
        return;
    }

    if (avcodec_parameters_to_context(input_codec_ctx, codecpar) < 0) {
        std::cerr << "Could not copy codec parameters" << std::endl;
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    if (avcodec_open2(input_codec_ctx, input_codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVFormatContext *output_format_ctx = nullptr;
    if (avformat_alloc_output_context2(&output_format_ctx, nullptr, nullptr, output_filename) < 0) {
        std::cerr << "Could not create output context" << std::endl;
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVStream *output_stream = avformat_new_stream(output_format_ctx, nullptr);
    if (!output_stream) {
        std::cerr << "Could not create output stream" << std::endl;
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!output_codec) {
        std::cerr << "Could not find encoder" << std::endl;
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVCodecContext *output_codec_ctx = avcodec_alloc_context3(output_codec);
    if (!output_codec_ctx) {
        std::cerr << "Could not allocate output codec context" << std::endl;
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    output_codec_ctx->width     = 1920;
    output_codec_ctx->height    = 1080;
    output_codec_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
    output_codec_ctx->time_base = {1, 30};

    if (avcodec_open2(output_codec_ctx, output_codec, nullptr) < 0) {
        std::cerr << "Could not open output codec" << std::endl;
        avcodec_free_context(&output_codec_ctx);
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    if (avcodec_parameters_from_context(output_stream->codecpar, output_codec_ctx) < 0) {
        std::cerr << "Could not copy output codec parameters" << std::endl;
        avcodec_free_context(&output_codec_ctx);
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_format_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            avcodec_free_context(&output_codec_ctx);
            avformat_free_context(output_format_ctx);
            avcodec_free_context(&input_codec_ctx);
            avformat_close_input(&input_format_ctx);
            return;
        }
    }

    if (avformat_write_header(output_format_ctx, nullptr) < 0) {
        std::cerr << "Error occurred when writing header" << std::endl;
        avcodec_free_context(&output_codec_ctx);
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVFrame *frame        = av_frame_alloc();
    AVFrame *scaled_frame = av_frame_alloc();
    if (!frame || !scaled_frame) {
        std::cerr << "Could not allocate frame" << std::endl;
        av_frame_free(&frame);
        av_frame_free(&scaled_frame);
        avcodec_free_context(&output_codec_ctx);
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    scaled_frame->format = output_codec_ctx->pix_fmt;
    scaled_frame->width  = output_codec_ctx->width;
    scaled_frame->height = output_codec_ctx->height;
    if (av_frame_get_buffer(scaled_frame, 0) < 0) {
        std::cerr << "Could not allocate scaled frame buffer" << std::endl;
        av_frame_free(&frame);
        av_frame_free(&scaled_frame);
        avcodec_free_context(&output_codec_ctx);
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    SwsContext *sws_ctx = sws_getContext(input_codec_ctx->width, input_codec_ctx->height, input_codec_ctx->pix_fmt,
                                         output_codec_ctx->width, output_codec_ctx->height, output_codec_ctx->pix_fmt,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        std::cerr << "Could not create scaling context" << std::endl;
        av_frame_free(&frame);
        av_frame_free(&scaled_frame);
        avcodec_free_context(&output_codec_ctx);
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&input_codec_ctx);
        avformat_close_input(&input_format_ctx);
        return;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    while (av_read_frame(input_format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            if (avcodec_send_packet(input_codec_ctx, &packet) < 0) {
                break;
            }

            while (avcodec_receive_frame(input_codec_ctx, frame) >= 0) {
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, input_codec_ctx->height,
                          scaled_frame->data, scaled_frame->linesize);

                scaled_frame->pts = av_rescale_q(frame->pts, input_codec_ctx->time_base, output_codec_ctx->time_base);

                if (avcodec_send_frame(output_codec_ctx, scaled_frame) < 0) {
                    break;
                }

                AVPacket output_packet;
                av_init_packet(&output_packet);
                output_packet.data = nullptr;
                output_packet.size = 0;

                while (avcodec_receive_packet(output_codec_ctx, &output_packet) >= 0) {
                    output_packet.stream_index = output_stream->index;
                    av_packet_rescale_ts(&output_packet, output_codec_ctx->time_base, output_stream->time_base);
                    av_interleaved_write_frame(output_format_ctx, &output_packet);
                    av_packet_unref(&output_packet);
                }
            }
        }
        av_packet_unref(&packet);
    }

    av_write_trailer(output_format_ctx);

    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_frame_free(&scaled_frame);
    avcodec_free_context(&output_codec_ctx);
}
