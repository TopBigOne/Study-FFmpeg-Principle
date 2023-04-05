//
// Created by dev on 2023/3/30.
//

#ifndef INC_11_1_SPLIT_SIMPLE_SPLIT_2_H
#define INC_11_1_SPLIT_SIMPLE_SPLIT_2_H

#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/bprint.h>

#define check_result(v, invoke_info){      \
    if(v<0){                      \
        fprintf(stderr,"invoke_info:%s : %s",invoke_info,av_err2str(v)) ;  \
        return v;\
        }             \
}
AVFormatContext *av_FormatContext = NULL;
AVCodecContext  *avCodecContext   = NULL;
AVCodec         *avCodec         = NULL;
AVFrame         *avFrame         = NULL;
AVFrame         *resultFrame     = NULL;
AVPacket        *avPacket        = NULL;

AVFilterContext *main_src_Context    = NULL;
AVFilterContext *scale_Context       = NULL;
AVFilterContext *overlay_Context     = NULL;
AVFilterContext *result_sink_Context = NULL;
AVFilterContext *split_Context       = NULL;
AVFilterGraph   *avFilterGraph       = NULL;

int RESULT      = 0;
int READ_END    = 0;
int FRAME_COUNT = 0;


int init_ffmpeg();


int is_audio_index(AVPacket *packet);

/**
 * contains avFrame and avPacket
 * @return
 */
int start_decoder();

int init_filter();

int save_yuv_to_file(AVFrame *frame, int count);

void free_all();

int start_task2();


#endif //INC_11_1_SPLIT_SIMPLE_SPLIT_2_H
