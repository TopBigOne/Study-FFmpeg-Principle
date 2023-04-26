//
// Created by dev on 2023/4/26.
//
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define VIDEO_PATH "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/27_parse_h264_NAL_with_mp4/juren-30s.mp4"
#define VIDEO_OUT_PATH "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/27_parse_h264_NAL_with_mp4/doc/result_video.mp4"

char *app_name = "牛 B";


int open_input(AVFormatContext **ctx, char *in_filename) {
    int ret = 0;
    if ((ret = avformat_open_input(ctx, in_filename, NULL, NULL)) < 0) {
        fprintf(stderr, "fail to open input %s\n", in_filename);
        return ret;
    }
    if ((ret = avformat_find_stream_info(*ctx, NULL)) < 0) {
        fprintf(stderr, "find stream info failed\n");
        return ret;
    }
    return ret;
}

int open_output(FILE **file, char *out_filename) {
    int ret = 0;
    *file = fopen(out_filename, "wb+");
    if (*file == NULL) {
        ret = -1;
        fprintf(stderr, "failed to open output %s\n", out_filename);
        return ret;
    }
    return ret;
}

int write_output(FILE *of, AVPacket *pkt) {
    if (pkt->size > 0) {
        size_t size = fwrite(pkt->data, 1, pkt->size, of);
        if (size <= 0) {
            fprintf(stderr, "fwrite failed\n");
            return -1;
        } else {
            fprintf(stdout, "write packet, size=%d\n", size);
        }
    }
    return 0;
}

int open_bitstream_filter(AVStream *stream, AVBSFContext **bsf_ctx, const char *name) {
    int                     ret     = 0;
    const AVBitStreamFilter *filter = av_bsf_get_by_name(name);
    if (!filter) {
        ret = -1;
        fprintf(stderr, "Unknow bitstream filter.\n");
    }

    if ((ret = av_bsf_alloc(filter, bsf_ctx) < 0)) {
        fprintf(stderr, "av_bsf_alloc failed\n");
        return ret;
    }

    if ((ret = avcodec_parameters_copy((*bsf_ctx)->par_in, stream->codecpar)) < 0) {
        fprintf(stderr, "avcodec_parameters_copy failed, ret=%d\n", ret);
        return ret;
    }

    if ((ret = av_bsf_init(*bsf_ctx)) < 0) {
        fprintf(stderr, "av_bsf_init failed, ret=%d\n", ret);
        return ret;
    }
    return ret;
}

int filter_stream(AVBSFContext *bsf_ctx, AVPacket *pkt, FILE *of, int eof) {

    int ret = 0;
    ret = av_bsf_send_packet(bsf_ctx, eof ? NULL : pkt) < 0;
    if (ret) {
        fprintf(stderr, "av_bsf_send_packet failed, ret=%d\n", ret);
        return ret;
    }
    while ((ret = av_bsf_receive_packet(bsf_ctx, pkt) == 0)) {
        ret = write_output(of, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            return ret;
        }
    }
    if (ret == AVERROR(EAGAIN)) {
        ret = 0;
    }
    return ret;
}


/**
 * ffmpeg命令将MP4中的H264裸流提取出来
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char **argv) {
    // app_name                           = argv[0];
    char *in_filename  = VIDEO_PATH;
    char *out_filename = VIDEO_OUT_PATH;
    FILE *of;

    int ret                = 0;
    int video_stream_index = -1;
    int is_annexb          = 1;

    AVFormatContext *ifmt_ctx = avformat_alloc_context();
    AVBSFContext    *bsf_ctx;


    if ((ret = open_input(&ifmt_ctx, in_filename)) < 0) {
        fprintf(stderr, "open_input failed, ret=%d\n", ret);
        goto end;
    }
    if ((ret = open_output(&of, out_filename)) < 0) {
        fprintf(stderr, "open_output failed, ret=%d\n", ret);
        goto end;
    }

    AVPacket *pkt = av_packet_alloc();


    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            is_annexb          = strcmp(av_fourcc2str(ifmt_ctx->streams[i]->codecpar->codec_tag), "avc1") == 0 ? 0 : 1;
            break;
        }
    }

    if (video_stream_index == -1) {
        fprintf(stderr, "no video stream found.\n");
        goto end;
    }

    fprintf(stdout, "is_annexb=%d\n", is_annexb);

    if (!is_annexb) {
        if (ret = open_bitstream_filter(ifmt_ctx->streams[video_stream_index], &bsf_ctx, "h264_mp4toannexb") < 0) {
            fprintf(stderr, "open_bitstream_filter failed, ret=%d\n", ret);
            goto end;
        }
    }

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != video_stream_index) {
            fprintf(stdout, "read a packet, not a video frame\n");
            continue;
        }
        if (is_annexb) {
            ret = write_output(of, pkt);
        } else {
            ret = filter_stream(bsf_ctx, pkt, of, 0);
        }
        if (ret < 0) {
            goto end;
        }


        av_packet_unref(pkt);
    }
    if (!is_annexb) {//flush bistream filter
        filter_stream(bsf_ctx, NULL, of, 1);
    }

    end:
    if (pkt)
        av_packet_free(&pkt);
    if (ifmt_ctx)
        avformat_close_input(&ifmt_ctx);
    if (bsf_ctx)
        av_bsf_free(&bsf_ctx);
    if (of)
        fclose(of);
    fprintf(stdout, "convert finished, ret=%d\n", ret);
    return 0;
}