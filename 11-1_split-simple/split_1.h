//
// Created by dev on 2023/3/30.
//

#ifndef INC_11_1_SPLIT_SIMPLE_SPLIT_1_H
#define INC_11_1_SPLIT_SIMPLE_SPLIT_1_H


#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/bprint.h>


int READ_END    = 0;
int RESULT      = -1;
int FRAME_COUNT = 0;

AVFormatContext *avFormatContext = NULL;
AVCodecContext  *avCodecContext  = NULL;
AVCodec         *avCodec         = NULL;
AVPacket        *avPacket        = NULL;
AVFrame         *avFrame         = NULL;
AVFrame         *result_frame    = NULL;

AVFilterGraph   *filter_graph    = NULL;
AVFilterContext *main_src_ctx    = NULL;
AVFilterContext *result_sink_ctx = NULL;


int InitFFmpeg();

int checkAudioIndex(AVPacket *packet);

int StartDecoder();

int InitFilter();

void save_yuv_to_file(AVFrame *tempAVFrame, int count);

int FreeAll();



int start_task();



#endif //INC_11_1_SPLIT_SIMPLE_SPLIT_1_H
