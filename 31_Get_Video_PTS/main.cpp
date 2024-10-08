#include <iostream>


extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/eval.h>
#include <libavutil/display.h>
}

#define  V_PATH "/Users/dev/Desktop/yu_fan/Feishu20240912_看看PTS.mp4"
//#define  V_PATH "/Users/dev/Desktop/yu_fan/Feishu20240912_看看PTS.mp4"

void testPointerReference() {
    puts("测试指针引用");
    AVPacket *ori_pkt = av_packet_alloc();
    ori_pkt->flags = 34;

    AVPacket *rv_pkt = av_packet_alloc();
    AVPacket *n_pkt = rv_pkt;



    av_packet_ref(n_pkt, ori_pkt);

    printf("ori_pkt flag : %d\n", ori_pkt->flags);
    printf("n_pkt flag   : %d\n", n_pkt->flags);
    printf("----------------------------------------\n");



    av_packet_unref(n_pkt);
    printf("ori_pkt flag : %d\n", ori_pkt->flags);
    printf("n_pkt flag   : %d\n", n_pkt->flags);
    printf("rv_pkt flag  : %d\n", rv_pkt->flags);
    printf("----------------------------------------\n");

}

void getVideoExtraData() {

    int             spsLength      = 0;
    int             ppsLength      = 0;
    AVFormatContext *formatContext = avformat_alloc_context();
    if (!formatContext) {
        // 处理内存分配错误
        return;
    }

    if (avformat_open_input(&formatContext, V_PATH, nullptr, nullptr) != 0) {
        // 处理打开文件错误
        return;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        // 处理查找流信息错误
        return;
    }

    AVCodec        *codec           = nullptr;
    AVCodecContext *codecContext    = nullptr;
    int            videoStreamIndex = -1;

    for (int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            codec            = avcodec_find_decoder(formatContext->streams[i]->codecpar->codec_id);
            if (!codec) {
                // 处理找不到解码器错误
                break;
            }
            codecContext = avcodec_alloc_context3(codec);
            if (avcodec_parameters_to_context(codecContext, formatContext->streams[i]->codecpar) < 0) {
                // 处理参数转换错误
                break;
            }

            uint8_t *sps_data = NULL;
            uint8_t *pps_data = NULL;
            int     sps_size  = 0;
            int     pps_size  = 0;

            sps_data = codecContext->extradata + 5; // Assuming SPS data starts at index 5
            sps_size = codecContext->extradata_size - 5; // Size of SPS data

            pps_data = sps_data + sps_size; // PPS data follows SPS data
            pps_size = codecContext->extradata_size - sps_size - 5; // Size of PPS data




            std::cout << "spsLength: " << spsLength << std::endl;
            std::cout << "ppsLength: " << ppsLength << std::endl;


            avcodec_open2(codecContext, codec, nullptr);
            break;
        }
    }

    AVPacket packet;
    av_init_packet(&packet);

    while (av_read_frame(formatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            AVFrame *frame = av_frame_alloc();
            int     ret    = avcodec_send_packet(codecContext, &packet);
            if (ret < 0) {
                // 处理发送数据包错误
                break;
            }

            ret = avcodec_receive_frame(codecContext, frame);
            if (ret == 0) {
                // 输出帧的 PTS 值
                //  std::cout << "PTS: " << frame->pts << std::endl;
            }

            av_frame_free(&frame);
        }
        av_packet_unref(&packet);
    }

    avformat_close_input(&formatContext);

}


int getVideoPTS() {
    AVFormatContext *formatContext = avformat_alloc_context();
    if (!formatContext) {
        // 处理内存分配错误
        return -1;
    }

    if (avformat_open_input(&formatContext, V_PATH, nullptr, nullptr) != 0) {
        // 处理打开文件错误
        return -1;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        // 处理查找流信息错误
        return -1;
    }

    AVCodec        *codec           = nullptr;
    AVCodecContext *codecContext    = nullptr;
    int            videoStreamIndex = -1;

    for (int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            codec            = avcodec_find_decoder(formatContext->streams[i]->codecpar->codec_id);
            if (!codec) {
                // 处理找不到解码器错误
                return -1;
            }
            codecContext = avcodec_alloc_context3(codec);
            if (avcodec_parameters_to_context(codecContext, formatContext->streams[i]->codecpar) < 0) {
                // 处理参数转换错误
                return -1;
            }
            avcodec_open2(codecContext, codec, nullptr);
            break;
        }
    }

    AVPacket packet;
    av_init_packet(&packet);

    while (av_read_frame(formatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            AVFrame *frame = av_frame_alloc();
            int     ret    = avcodec_send_packet(codecContext, &packet);
            if (ret < 0) {
                // 处理发送数据包错误
                return -1;
            }

            ret = avcodec_receive_frame(codecContext, frame);
            if (ret == 0) {
                // 输出帧的 PTS 值
                std::cout << "PTS: " << frame->pts << std::endl;
            }

            av_frame_free(&frame);
        }
        av_packet_unref(&packet);
    }

    avformat_close_input(&formatContext);

}

int main() {
    // getVideoPTS();
    //getVideoExtraData();
    testPointerReference();
    return 0;
}
