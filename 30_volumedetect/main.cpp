#include <iostream>
#include <string>

#include <stdio.h>

extern "C" {

#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>

}


void log_callback(void *ptr, int level, const char *fmt, va_list vargs) {

    if (level != AV_LOG_INFO) {
        return;
    }
    // case 1：
    vfprintf(stdout, fmt, vargs);

}


int main() {


    const char      *input_filename = "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/30_volumedetect/doc/juren-30s.mp4";
    AVFormatContext *fmt_ctx        = nullptr;
    AVCodecContext  *dec_ctx        = nullptr;

    AVFilterContext *buffersink_ctx    = nullptr;
    AVFilterContext *buffersrc_ctx     = nullptr;
    AVFilterGraph   *filter_graph      = nullptr;
    int             audio_stream_index = -1;

    fmt_ctx = avformat_alloc_context();

    av_log_set_callback(log_callback);
    av_log_set_level(AV_LOG_INFO);


    // step 1:
    if (avformat_open_input(&fmt_ctx, input_filename, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file" << std::endl;
        return -1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return -1;
    }

    // Find the audio stream
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    if (audio_stream_index == -1) {
        std::cerr << "Could not find audio stream" << std::endl;
        return -1;
    }

    AVCodec *dec = avcodec_find_decoder(fmt_ctx->streams[audio_stream_index]->codecpar->codec_id);
    if (!dec) {
        std::cerr << "Failed to find codec" << std::endl;
        return -1;
    }

    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) {
        std::cerr << "Failed to allocate the codec context" << std::endl;
        return -1;
    }

    if (avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[audio_stream_index]->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters to decoder context" << std::endl;
        return -1;
    }

    if (avcodec_open2(dec_ctx, dec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        return -1;
    }

    // Create filter graph
    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        std::cerr << "Could not allocate filter graph" << std::endl;
        return -1;
    }

    const AVFilter *abuffersrc   = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink  = avfilter_get_by_name("abuffersink");
    const AVFilter *volumedetect = avfilter_get_by_name("volumedetect");

    if (!abuffersrc || !abuffersink || !volumedetect) {
        std::cerr << "  Filter not found" << std::endl;
        return -1;
    }

    char args[512];
    snprintf(args, sizeof(args),
             "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
             fmt_ctx->streams[audio_stream_index]->time_base.num,
             fmt_ctx->streams[audio_stream_index]->time_base.den,
             dec_ctx->sample_rate,
             av_get_sample_fmt_name(dec_ctx->sample_fmt),
             dec_ctx->channel_layout);

    if (avfilter_graph_create_filter(&buffersrc_ctx, abuffersrc, "in", args, nullptr, filter_graph) < 0) {
        std::cerr << "Cannot create buffer source" << std::endl;
        return -1;
    }

    if (avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out", nullptr, nullptr, filter_graph) < 0) {
        std::cerr << "Cannot create buffer sink" << std::endl;
        return -1;
    }

    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;

    // 用于解析和构建过滤器图形。它接受一个过滤器图形、过滤器链的描述字符串和输入输出参数的列表，并根据描述字符串创建和连接相应的过滤器。
    if (avfilter_graph_parse_ptr(filter_graph, "volumedetect", &inputs, &outputs, nullptr) < 0) {
        std::cerr << "Cannot parse filter graph" << std::endl;
        return -1;
    }

    // 用于配置过滤器图形中的过滤器链。它会根据已经连接好的过滤器链，进行必要的参数设置和初始化工作，使得过滤器链准备就绪，可以进行数据处理
    if (avfilter_graph_config(filter_graph, nullptr) < 0) {
        std::cerr << "Cannot configure filter graph" << std::endl;
        return -1;
    }

    AVPacket packet;
    AVFrame  *frame      = av_frame_alloc();
    AVFrame  *filt_frame = av_frame_alloc();


    while (av_read_frame(fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == audio_stream_index) {
            if (avcodec_send_packet(dec_ctx, &packet) < 0) {
                av_packet_unref(&packet);
                continue;
            }

            while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    std::cerr << "Error while feeding the audio filtergraph" << std::endl;
                    break;
                }

                while (av_buffersink_get_frame(buffersink_ctx, filt_frame) >= 0) {
                    // Process filtered frame
                }
                av_frame_unref(filt_frame);
            }
        }
        av_packet_unref(&packet);
    }




    // Log volume information
    char *vol_stats = avfilter_graph_dump(filter_graph, nullptr);
    std::cout << "filter_graph：" << std::endl;
    std::cout << vol_stats << std::endl;

    av_free(vol_stats);

    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
}


