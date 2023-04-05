#include <stdio.h>

#include <libavutil/bprint.h>
#include <libavutil/pixdesc.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>

#define video_path "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/17-format_filter/video/juren-30s.mp4"

void init_format_filter();

void save_yuv_to_file(AVFrame *frame, int num);

void start_decoder();

void init_ffmpeg();

void printf_error(int result);

void free_all();

int err;
int read_end  = 0;
int frame_num = 0;
int result    = 1;

AVFormatContext *avFormatContext = NULL;
AVCodecContext  *avCodecContext  = NULL;
AVCodec         *avCodec         = NULL;
AVFrame         *avFrame         = NULL;
AVFrame         *resultFrame     = NULL;
AVPacket        *avPacket        = NULL;
// filter
AVFilterContext *main_src_ctx    = NULL;
AVFilterContext *buffersin_ctx   = NULL;
AVFilterGraph   *avFilterGraph   = NULL;

const AVPixFmtDescriptor *origin_fmt = NULL;
const AVPixFmtDescriptor *result_fmt = NULL;

int main() {
    init_ffmpeg();
    start_decoder();
    free_all();
    return 0;
}


void init_ffmpeg() {
    avFormatContext = avformat_alloc_context();
    result          = avformat_open_input(&avFormatContext, video_path, NULL, NULL);
    if (result < 0) {
        printf_error(result);
        return;
    }

    avCodecContext = avcodec_alloc_context3(NULL);
    result         = avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[0]->codecpar);
    if (result < 0) {
        printf_error(result);
        return;
    }

    avCodec = avcodec_find_decoder(avCodecContext->codec_id);

    result = avcodec_open2(avCodecContext, avCodec, NULL);
    if (result != 0) {
        printf_error(result);
        return;
    }

    avPacket    = av_packet_alloc();
    avFrame     = av_frame_alloc();
    resultFrame = av_frame_alloc();

}

void printf_error(int temp_result) {
    fprintf(stderr, "ERROR:%s\n", av_err2str(temp_result));
}

void start_decoder() {
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
            if ((result = avcodec_send_packet(avCodecContext, avPacket)) == AVERROR(EAGAIN)) {
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

            // case 1:
            if (result >= 0)

                init_format_filter();
            result = av_buffersrc_add_frame_flags(main_src_ctx, avFrame, AV_BUFFERSRC_FLAG_PUSH);
            if (result < 0) {
                printf_error(result);
            }

            result = av_buffersink_get_frame_flags(buffersin_ctx, resultFrame, AV_BUFFERSRC_FLAG_PUSH);
            if (result >= 0) {
                result_fmt = av_pix_fmt_desc_get(resultFrame->format);
                printf("result frame fmt is :  %d , %s\n", resultFrame->format, result_fmt->name);
                save_yuv_to_file(resultFrame, frame_num);
                av_frame_unref(avFrame);
                av_frame_unref(resultFrame);
            }

            frame_num++;
            if (frame_num >= 10) {
                return;
            }


        }

    }

}


void save_yuv_to_file(AVFrame *frame, int num) {
    //拼接文件名
    char yuv_pic_name[200] = {0};
    sprintf(yuv_pic_name,
            "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/17-format_filter/doc/yuv420p_%d.yuv", num);

    //写入文件
    FILE *fp = NULL;
    fp = fopen(yuv_pic_name, "wb+");
    fwrite(frame->data[0], 1, frame->linesize[0] * frame->height, fp);
    fwrite(frame->data[1], 1, frame->linesize[1] * frame->height / 2, fp);
    fwrite(frame->data[2], 1, frame->linesize[2] * frame->height / 2, fp);
    fclose(fp);

}

void init_format_filter() {
    if (avFilterGraph != NULL) {
        return;
    }
    AVFilterInOut *input;
    AVFilterInOut *output;
    avFilterGraph = avfilter_graph_alloc();

    AVRational tb  = avFormatContext->streams[0]->time_base;
    AVRational fr  = av_guess_frame_rate(avFormatContext, avFormatContext->streams[0], NULL);
    AVRational sar = avFrame->sample_aspect_ratio;

    origin_fmt = av_pix_fmt_desc_get(avFrame->format);
    printf("origin frame fmt is %d,%s \n", avFrame->format, origin_fmt->name);


    AVBPrint args;
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);

    av_bprintf(&args,
               "buffer=video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d[main];"
               "[main]format=pix_fmts=yuyv422[result];"
               "[result]buffersink",
               avFrame->width, avFrame->height, avFrame->format, tb.num, tb.den, sar.num, sar.den, fr.num,
               fr.den);


    // parse the filter str.
    result = avfilter_graph_parse2(avFilterGraph, args.str, &input, &output);
    if (result < 0) {
        printf_error(result);
        return;
    }
    result = avfilter_graph_config(avFilterGraph,NULL);
    main_src_ctx  = avfilter_graph_get_filter(avFilterGraph, "Parsed_buffer_0");
    buffersin_ctx = avfilter_graph_get_filter(avFilterGraph, "Parsed_buffersink_2");
}


void free_all() {
    av_frame_free(&avFrame);
    av_frame_free(&resultFrame);
    av_packet_free(&avPacket);

    //关闭编码器，解码器。
    avcodec_close(avCodecContext);

    //释放容器内存。
    avformat_free_context(avFormatContext);
    printf("done \n");

    //释放滤镜。
    avfilter_graph_free(&avFilterGraph);


}
