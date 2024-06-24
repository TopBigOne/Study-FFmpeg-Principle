//
// Created by dev on 2024/6/24.
//

#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

}

int convert_video3(const char *input_filename, const char *output_filename) {
    // Initialize FFmpeg libraries
    av_register_all();
    avformat_network_init();

    // Open the input video file
    AVFormatContext *formatContext = nullptr;
    if (avformat_open_input(&formatContext, input_filename, nullptr, nullptr) != 0) {
        std::cerr << "Failed to open input file" << std::endl;
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Failed to retrieve stream information" << std::endl;
        return -1;
    }

    // Find the video stream
    int               videoStreamIndex = -1;
    AVCodecParameters *codecParams     = nullptr;
    for (int          i                = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            codecParams      = formatContext->streams[i]->codecpar;
            break;
        }
    }

    if (videoStreamIndex == -1 || codecParams == nullptr) {
        std::cerr << "Failed to find video stream" << std::endl;
        return -1;
    }

    // Find the decoder for the video stream
    AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
    if (codec == nullptr) {
        std::cerr << "Failed to find video decoder" << std::endl;
        return -1;
    }

    // Create a codec context and open the codec
    AVCodecContext *codecContext = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(codecContext, codecParams) < 0) {
        std::cerr << "Failed to copy codec parameters to context" << std::endl;
        return -1;
    }
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::cerr << "Failed to open video codec" << std::endl;
        return -1;
    }
    // Set encoder timebase


    // Create a scaler context for resizing
    SwsContext *scalerContext = sws_getContext(codecContext->width, codecContext->height, codecContext->pix_fmt,
                                               1920, 1080, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (scalerContext == nullptr) {
        std::cerr << "Failed to create scaler context" << std::endl;
        return -1;
    }

    // Open the output video file
    AVFormatContext *outputFormatContext = nullptr;
    if (avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, output_filename) < 0) {
        std::cerr << "Failed to create output format context" << std::endl;
        return -1;
    }



    // Add a video stream to the output file
    AVStream *outputVideoStream = avformat_new_stream(outputFormatContext, nullptr);
    if (outputVideoStream == nullptr) {
        std::cerr << "Failed to create output video stream" << std::endl;
        return -1;
    }


    // Copy codec parameters from the input video stream
    if (avcodec_parameters_copy(outputVideoStream->codecpar, codecParams) < 0) {
        std::cerr << "Failed to copy codec parameters to output stream" << std::endl;
        return -1;
    }

    // Open the output video codec
    AVCodec *outputCodec = avcodec_find_encoder(outputVideoStream->codecpar->codec_id);
    if (outputCodec == nullptr) {
        std::cerr << "Failed to find output video codec" << std::endl;
        return -1;
    }

    AVCodecContext *outputCodecContext = avcodec_alloc_context3(outputCodec);
    if (avcodec_parameters_to_context(outputCodecContext, outputVideoStream->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters to output codec context" << std::endl;
        return -1;
    }

    outputCodecContext->time_base = {1, 30};


    if (avcodec_open2(outputCodecContext, outputCodec, nullptr) < 0) {
        std::cerr << "Failed to open output video codec" << std::endl;
        return -1;
    }



    // Create an output video frame
    AVFrame *outputFrame = av_frame_alloc();
    if (outputFrame == nullptr) {
        std::cerr << "Failed to allocate output video frame" << std::endl;
        return -1;
    }
    outputFrame->format = outputCodecContext->pix_fmt;
    outputFrame->width  = outputCodecContext->width;
    outputFrame->height = outputCodecContext->height;
    if (av_frame_get_buffer(outputFrame, 0) < 0) {
        std::cerr << "Failed to allocate output frame data" << std::endl;
        return -1;
    }

    // Read and process video frames
    AVPacket packet;
    while (av_read_frame(formatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            // Decode the video packet
            AVFrame *inputFrame = av_frame_alloc();
            if (inputFrame == nullptr) {
                std::cerr << "Failed to allocate input video frame" << std::endl;
                return -1;
            }
            int frameFinished;
            avcodec_decode_video2(codecContext, inputFrame, &frameFinished, &packet);
            if (frameFinished) {
                // Resize the video frame
                sws_scale(scalerContext, inputFrame->data, inputFrame->linesize, 0, codecContext->height,
                          outputFrame->data, outputFrame->linesize);

                // Encode and write the output frame
                AVPacket outputPacket;
                av_init_packet(&outputPacket);
                outputPacket.data = nullptr;
                outputPacket.size = 0;
                int gotOutput;
                avcodec_encode_video2(outputCodecContext, &outputPacket, outputFrame, &gotOutput);
                if (gotOutput) {
                    outputPacket.stream_index = outputVideoStream->index;
                    av_packet_rescale_ts(&outputPacket, outputCodecContext->time_base, outputVideoStream->time_base);
                    av_interleaved_write_frame(outputFormatContext, &outputPacket);
                    av_packet_unref(&outputPacket);
                }
            }
            av_frame_free(&inputFrame);
        }

        av_packet_unref(&packet);
    }

    // Write the output file trailer
    av_write_trailer(outputFormatContext);

    // Clean up resources
    av_frame_free(&outputFrame);
    avcodec_free_context(&codecContext);
    avcodec_free_context(&outputCodecContext);
    sws_freeContext(scalerContext);
    avformat_close_input(&formatContext);
    avformat_free_context(outputFormatContext);

    return 0;
}


extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <iostream>



void log_callback(void* ptr, int level, const char* fmt, va_list vargs) {
    if (level <= AV_LOG_INFO) {
        char log_message[1024];
        vsnprintf(log_message, sizeof(log_message), fmt, vargs);
        std::cout << log_message;
    }
}


void convert_video4(const char *input_filename, const char *output_filename) {

    av_register_all();
    av_log_set_level(AV_LOG_INFO);
    av_log_set_callback(log_callback);

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

    int ret;
    while (av_read_frame(input_format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            ret = avcodec_send_packet(input_codec_ctx, &packet);
            if (ret < 0) {
                std::cerr << "Error sending a packet for decoding." << std::endl;
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(input_codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error during decoding." << std::endl;
                    break;
                }

                sws_scale(sws_ctx, frame->data, frame->linesize, 0, input_codec_ctx->height, scaled_frame->data, scaled_frame->linesize);
                scaled_frame->pts = av_rescale_q(frame->pts, input_format_ctx->streams[video_stream_index]->time_base, output_codec_ctx->time_base);

                ret = avcodec_send_frame(output_codec_ctx, scaled_frame);
                if (ret < 0) {
                    std::cerr << "Error sending a frame for encoding." << std::endl;
                    break;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_packet(output_codec_ctx, &packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        std::cerr << "Error during encoding." << std::endl;
                        break;
                    }

                    av_packet_rescale_ts(&packet, output_codec_ctx->time_base, output_stream->time_base);
                    packet.stream_index = output_stream->index;

                    ret = av_interleaved_write_frame(output_format_ctx, &packet);
                    if (ret < 0) {
                        std::cerr << "Error muxing packet." << std::endl;
                        break;
                    }

                    av_packet_unref(&packet);
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
    avformat_free_context(output_format_ctx);
    avcodec_free_context(&input_codec_ctx);
    avformat_close_input(&input_format_ctx);

    if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_ctx->pb);
    }

    std::cout << "Video conversion completed successfully!" << std::endl;
}