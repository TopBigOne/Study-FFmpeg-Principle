/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * simple media player based on the FFmpeg libraries
 */

#include "config.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/fifo.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavutil/bprint.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER

# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"

#endif

#include <SDL.h>
#include <SDL_thread.h>

#include "cmdutils.h"

#include <assert.h>

const char program_name[]     = "ffplay";
const int  program_birth_year = 2003;

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* TMD ,这 还同步个屁啊....no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

static unsigned sws_flags = SWS_BICUBIC;

typedef struct MyAVPacketList {
    // 解封装后的数据
    AVPacket *pkt;
    // 播放序列，它的值是 PacketQueue 里的 serial 赋值过来的；
    int      serial;
}               MyAVPacketList;

/**
 * 用AVFifoBuffer 实现的一个队列，实际保存的是 AVPacket，这是，这个AVPacket，有一个序号；
 */
typedef struct PacketQueue {
    // 存储的是 MyAVPacketList，实际保存的是 AVPacket，这是，这个AVPacket，有一个序号；
    AVFifoBuffer *pkt_list;
    //包数量，代表队列里面有多少个 AVPacket。
    int          nb_packets;
    // 队列所有元素的 数据大小总和:算法是所有的 AVPacket 本身的大小 加上 AVPacket->size
    int          size;
    //队列所有元素的数据播放持续时间 ？ 通过累加 队列里所有的 AVPacket->duration 得到。
    int64_t      duration;
    // 用户退出请求标志 ： 只有 0 和 1 ，两个值；
    // audio_thread() 跟 video_thread() 解码线程就会退出
    int          abort_request;
    //播放序列号:队列的序号，每次跳转播放时间点 ，serial 就会 +1。
    // 另一个数据结构 MyAVPacketList 里面也有一个 serial 字段。
    /*
     * 两个 serial 通过比较匹配,来丢弃无效的缓存帧，什么情况会导致队列的缓存帧无效？
     * 跳转播放时间点的时候。
     * 例如:
     * 此时此刻，PacketQueue 队列里面缓存了 8 个帧，
     * 但是这 8 个帧都 第30分钟 才开始播放的，
     * 如果你通过 ➔ 按键前进到 第35分钟 的位置播放，那队列的 8 个缓存帧 就无效了，需要丢弃。
     * 由于每次跳转播放时间点， PacketQueue::serial 都会 +1 ，
     * 而 MyAVPacketList::serial 的值还是原来的，两个 serial 不一样，就会丢弃帧。
     * */
    int          serial;
    //用于维持PacketQueue的多线程安全,主要用于修改队列的时候加锁。
    SDL_mutex    *mutex;
    // 用于读、写线程相互通知，用于 read_thread() 线程 跟 audio_thread() ，video_thread() 线程 进行通信的。
    SDL_cond     *cond;
}               PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3 // 视频队列
#define SUBPICTURE_QUEUE_SIZE 16 // 字幕队列
#define SAMPLE_QUEUE_SIZE 9  // 采样队列
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
    //采样率
    int                 freq;
    //通道数
    int                 channels;
    //通道布局，如：2.1声道，5.1声道
    int64_t             channel_layout;
    //音频采样格式，如：AV_SAMPLE_FMT_S16
    enum AVSampleFormat fmt;
    //一个采样单元占用的字节数
    int                 frame_size;
    //一秒时间的字节数
    int                 bytes_per_sec;
}               AudioParams;

/**
 * Clock 时钟封装,记录音频流，视频流当前播放到哪里的
 */
typedef struct Clock {
    //时钟基础, 当前帧(待播放)显示时间戳，播放后,当前帧变成上一帧,时间是s:秒
    double pts;           /* clock base */
    //当前 pts 与 当前系统时钟的差值, audio、video对于该值是独立的
    double pts_drift;     /* clock base minus time at which we updated the clock */
    //最后一次更新的系统时钟
    double last_updated;
    // 时钟速度控制，用于控制播放速度
    double speed;
    // 播放序列，所谓播放序列就是一段连续的播放动作，一个seek操作会启动一段新的播放序列
    int    serial;           /* clock is based on a packet with this serial */
    // = 1 说明是暂停状态
    int    paused;
    //指向packet_serial  :obsolete(废弃的), detection: 检测
    int    *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
}     Clock;



/**
 *  Frame是音频、视频、字幕通用的结构体，AVFrame是真正存储解码后的音视频数据，存储字幕使用AVSubtitle;
 *  Common struct for handling all types of decoded data and allocated render buffers.
 */
typedef struct Frame {
    // 指向数据帧，音视频解码后的数据
    AVFrame    *frame;
    // 用于字幕
    AVSubtitle sub;
    //播放序列，在seek时serial会变化
    int        serial;
    //时间戳，单位为秒
    double     pts;           /* presentation timestamp for the frame */

    //该帧持续时间，单位为秒
    double     duration;      /* estimated duration of the frame */
    //该帧在输入文件中的字节位置
    int64_t    pos;          /* byte position of the frame in the input file */
    int        width;
    int        height;
    int        format;
    // sar： 横竖采样比
    AVRational sar;
    //记录该帧是否已经显示过
    int        uploaded;
    // 1旋转180，0正常播放
    int        flip_v;
}               Frame;

/**
 * FrameQueue是一个环形缓冲区（ring buffer），是用数组实现 的一个FIFO，
 * ffplay中创建了音频frame_queue、视频frame_queue、字幕frame_queue，
 * 每一个frame_queue都有一个写端和一个读端，
 *  * 写端位于解码线程，
 *  * 读端位于播放线程。
 * FrameQueue操作提供以下方法：
　　（a）frame_queue_unref_item：释放Frame⾥⾯的AVFrame和 AVSubtitle
　　（b）frame_queue_init：初始化队列
 */
typedef struct FrameQueue {
    //队列大小，数字太大时占用内存就会越大，需要注意设置
    Frame       queue[FRAME_QUEUE_SIZE];
    // 读索引，这其实是用来读取上一帧已经播放的AVFrame的。
    // rindex+rindex_shown，这个是用来读取下一个准备播放的 AVFrame 的
    int         rindex;
    //写索引
    int         windex;
    //当前总帧数，这个字段不是内存大小，而是个数，代表 当前队列已经缓存了多少个 Frame。
    int         size;
    //可存储最大帧数，是一个定值；
    int         max_size;
    //=1 说明要在队列里面保持最后一帧的数据不释放，只在销毁队列的时候才真正释放
    //keep_last 代表 播放之后是否保存上一帧在队列里面不销毁
    int         keep_last;
    //初始化值为0，配合kepp_last=1使用
    int         rindex_shown;

    SDL_mutex   *mutex;
    SDL_cond    *cond;
    // FrameQueue 的数据是从哪一个 PacketQueue 里来的。
    PacketQueue *pktq;
}               FrameQueue;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,/*按照视频同步*/
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external（外部时钟） clock */
};

/**
 * Decoder 解码器封装
 */
typedef struct Decoder {
    char decode_name [64];
    // 要进行解码的 AVPacket，也是要发送给解码器的 AVPacket
    // 这个实际上就是从 AVPacket 队列拿出来的。然后把这个 pkt 发送给解码器，
    // 如果发送成功，那当然是 unref 这个 pkt，但是如果发送给解码器失败，
    // 就会把 packet_pending 置为1，pkt 不进行 unref，下次再继续发送。
    AVPacket       *pkt;
    // AVPacket 队列，音频归音频、视频归视频的,用 AVFifoBuffer  实现的。
    PacketQueue    *queue;
    // 解码器上下文
    AVCodecContext *avctx;
    //包序列 ： 记录的是解码器上一次用来解码的 AVPacket 的序列号
    int            pkt_serial;

    // 已完成的时候，finished 等于上面的 pkt_serial。
    // 当 buffersink 输出 EOF 的时候就是已完成。
    //=0解码器处理工作状态，!=0处于空闲状态
    int finished;

    // //代表上一个 AVPacket 已经从队列取出来了，但是未发送成功给解码器。
    // 未发生成功的会保留在第一个字段 pkt 里面，下次会直接发送，不从队列取。
    //=0解码器处于异常状态，需要考虑重置解码器，=1解码器处于正常状态
    int packet_pending;

    //条件变量，AVPacket 队列已经没有数据的时候会激活这个条件变量。
    // 检查到packet队列为空时,发送signal,缓存 read_thread 读取数据
    SDL_cond   *empty_queue_cond;
    // 流的第一帧的pts
    int64_t    start_pts;
    // 流的第一帧的pts的时间基
    AVRational start_pts_tb;
    // 下一帧的pts，只有音频用到这个 next_pts 字段
    // next_pts 的计算规则就是上一帧的 pts 加上他的样本数（也就是播放多久）
    int64_t    next_pts;
    // 下一帧的pts的时间基
    AVRational next_pts_tb;
    // 线程句柄
    SDL_Thread *decoder_tid;
}               Decoder;

/**
 * 播放器的全局管理器
 */
typedef struct VideoState {
    // 读线程句柄
    SDL_Thread    *read_tid;
    // 指向demuxer，解复用器格式：dshow、flv
    AVInputFormat *iformat;

    // =1请求退出播放
    int abort_request;
    //=1需要刷新画面
    int force_refresh;
    //=1暂停，=0播放
    int paused;
    //保存暂停/播放状态
    int last_paused;
    //mp3、acc音频文件附带的专辑封面，所以需要注意的是音频文件不一定只存在音频流本身
    int queue_attachments_req;
    //标识一次seek请求
    int seek_req;
    // seek标志，按字节还是时间seek，诸如AVSEEK_BYTE等
    int seek_flags;

    //请求seek的目标位置（当前位置+增量）
    int64_t         seek_pos;
    //本次seek的增量
    int64_t         seek_rel;
    int             read_pause_return;
    //iformat的上下文
    AVFormatContext *ic;
    int             realtime;

    // 三种同步的时钟
    Clock audclk; //音频时钟 : 记录音频流的目前的播放时刻 。
    Clock vidclk; //视频时钟  : 记录视频流的目前的播放时刻
    // 外部时钟 : 取第一帧 音频 或 视频的 pts 作为 起始时间，然后随着 物理时间 的消逝增长，
    // 所以是物理时间的当前时刻。到底是以音频的第一帧，
    // 还是视频的第一帧？取决于 av_read_frame() 函数第一次读到的是音频还是视频。
    Clock extclk;

    //视频frame队列
    FrameQueue pictq;
    //字幕frame队列
    FrameQueue subpq;
    //采样frame队列
    FrameQueue sampq;

    // 音频解码器
    Decoder auddec;
    //视频解码器
    Decoder viddec;
    //字幕解码器
    Decoder subdec;

    //音频流索引
    int audio_stream;

    //音视频同步类型，默认audio master
    int av_sync_type;

    //当前音频帧的pts + 当前帧Duration
    double audio_clock;
    // audio 播放序列，seek可改变此值
    // 只是一个用做临时用途的变量，实际上存储的就是 AVFrame 的 serial 字段。
    // 不用特别关注。而视频直接用的 AVFrame 的 serial
    int    audio_clock_serial;
    /* 以下四个参数，非audio master 同步方式使用*/
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int    audio_diff_avg_count;

    // 音频流
    AVStream     *audio_st;
    // 音频pack额头队列
    PacketQueue  audioq;
    // sdl 音频缓冲区大小
    int          audio_hw_buf_size;
    // 指向需要重采样的数据
    uint8_t      *audio_buf;
    // 指向重采样以后得数据
    uint8_t      *audio_buf1;
    // 待播放的一帧音频数据（audio buf）大小
    unsigned int audio_buf_size; /* in bytes */
    // 申请到的音频缓冲区audio_buf1实际大小
    unsigned int audio_buf1_size;

    //更新拷贝位置，当前音频帧中已拷贝入SDL音频缓冲区的位置索引
    int audio_buf_index; /* in bytes */

    //当前音频帧中尚未拷贝入SDL音频缓冲区的数据量
    int audio_write_buf_size;
    // 音量 大小
    int audio_volume;
    // =1静音，=0正常
    int muted;


    // 音频frame的参数
    struct AudioParams audio_src;
#if CONFIG_AVFILTER
    struct AudioParams audio_filter_src;
#endif
    //SDL支持的音频参数，重采样转换
    struct AudioParams audio_tgt;
    //音频重采样context
    struct SwrContext  *swr_ctx;

    //丢弃视频packet计数
    int         frame_drops_early;
    //丢弃视频frame计数
    int         frame_drops_late;

    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    }           show_mode;
    // 音频波形显示使用
    int16_t     sample_array[SAMPLE_ARRAY_SIZE];
    int         sample_array_index;
    int         last_i_start;
    RDFTContext *rdft;
    int         rdft_bits;
    FFTSample   *rdft_data;
    int         xpos;
    double      last_vis_time;
    SDL_Texture *vis_texture;
    // 字幕显示
    SDL_Texture *sub_texture;
    // 视频显示
    SDL_Texture *vid_texture;

    // 字幕流索引
    int         subtitle_stream;
    //字幕流
    AVStream    *subtitle_st;
    //字幕packet队列
    PacketQueue subtitleq;

    // 记录最后一帧播放时间
    double frame_timer;
    double frame_last_returned_time;

    // 滤镜容器处理上一帧所花的时间，这是一个预估值
    double frame_last_filter_delay;
    //视频流索引
    int    video_stream;

    //视频流
    AVStream    *video_st;
    //视频packet队列
    PacketQueue videoq;
    // 一帧最大的间隔: 也表示 一帧画面，最大的持续时间
    double      max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity

    struct SwsContext *img_convert_ctx;
    // 字幕尺寸格式变换
    struct SwsContext *sub_convert_ctx;
    //是否读取结束
    int               eof;

    //文件名
    char *filename;
    //宽，高，x起始坐标，y起始坐标
    int  width, height, xleft, ytop;
    //=1步进播放模式，=0其他模式
    int  step;

#if CONFIG_AVFILTER
    int             vfilter_idx;
    AVFilterContext *in_video_filter;   // the first filter in the video chain
    AVFilterContext *out_video_filter;  // the last filter in the video chain
    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
    AVFilterGraph   *agraph;              // audio filter graph
#endif

    // 代表最后一个audio、video、subtitle流
    int last_video_stream, last_audio_stream, last_subtitle_stream;

    // 当读取线程队列满后进入休眠，可通过condition唤醒读取线程
    SDL_cond *continue_read_thread;
}               VideoState;

/* options specified by the user */
static AVInputFormat *file_iformat;
static const char    *input_filename;
static const char    *window_title;
static int           default_width                        = 640;
static int           default_height                       = 480;
static int           screen_width                         = 0;
static int           screen_height                        = 0;
static int           screen_left                          = SDL_WINDOWPOS_CENTERED;
static int           screen_top                           = SDL_WINDOWPOS_CENTERED;
static int           audio_disable;
static int           video_disable;
static int           subtitle_disable;
static const char    *wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};
static int           seek_by_bytes                        = -1;
static float         seek_interval                        = 10;
static int           display_disable;
static int           borderless;
static int           alwaysontop;
static int           startup_volume                       = 100;
static int           show_status                          = -1;
/*按照 音频来同步音视频*/
static int           av_sync_type                         = AV_SYNC_AUDIO_MASTER;
static int64_t       start_time                           = AV_NOPTS_VALUE;
static int64_t       duration                             = AV_NOPTS_VALUE;
static int           fast                                 = 0;
static int           genpts                               = 0;
static int           lowres                               = 0;
static int           decoder_reorder_pts                  = -1;
static int           autoexit;
static int           exit_on_keydown;
static int           exit_on_mousedown;
static int           loop                                 = 1;
static int           framedrop                            = -1;
static int           infinite_buffer                      = -1;
static enum ShowMode show_mode                            = SHOW_MODE_NONE;
static const char    *audio_codec_name;
static const char    *subtitle_codec_name;
static const char    *video_codec_name;
double               rdftspeed                            = 0.02;
static int64_t       cursor_last_shown;
static int           cursor_hidden                        = 0;
#if CONFIG_AVFILTER
static const char **vfilters_list = NULL;
static int        nb_vfilters     = 0;
static char       *afilters       = NULL;
#endif
static int autorotate       = 1;
static int find_stream_info = 1;
static int filter_nbthreads = 0;

/* current context */
static int     is_full_screen;
static int64_t audio_callback_time;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static SDL_Window        *window;
static SDL_Renderer      *renderer;
static SDL_RendererInfo  renderer_info            = {0};
static SDL_AudioDeviceID audio_dev;

static const struct TextureFormatEntry {
    enum AVPixelFormat format;
    int                texture_fmt;
}
sdl_texture_format_map[] = {
        {AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332},
        {AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444},
        {AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555},
        {AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555},
        {AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565},
        {AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565},
        {AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24},
        {AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24},
        {AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888},
        {AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888},
        {AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888},
        {AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888},
        {AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888},
        {AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888},
        {AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888},
        {AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888},
        {AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV},
        {AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2},
        {AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY},
        {AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN},
};

#if CONFIG_AVFILTER

/**
 * 处理 -vf 命令行参数的
 * @param optctx
 * @param opt
 * @param arg
 * @return
 */
static int opt_add_vfilter(void *optctx, const char *opt, const char *arg) {
    GROW_ARRAY(vfilters_list, nb_vfilters);
    vfilters_list[nb_vfilters - 1] = arg;
    return 0;
}

#endif

static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2) {
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels) {
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}

/**
 * AVPacket 放进 PacketQueue 真正的实现
 * @param q
 * @param pkt
 * @return
 */
static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt) {
    MyAVPacketList pkt1;

    if (q->abort_request)
        return -1;

    // 判断 AVFifoBuffer 里面还有多少内存空间可以写？
    // 检查队列剩余空间是否 够用？
    if (av_fifo_space(q->pkt_list) < sizeof(pkt1)) {
        // 不够用的话，扩容一个pkt1 大小；
        if (av_fifo_grow(q->pkt_list, sizeof(pkt1)) < 0)
            return -1;
    }

    // 赋值操作
    pkt1.pkt    = pkt;
    pkt1.serial = q->serial;

    // 往 AVFifoBuffer 里面写内存数据
    av_fifo_generic_write(q->pkt_list, &pkt1, sizeof(pkt1), NULL);
    // 队列packet数量+1
    q->nb_packets++;
    // 队列所有元素的数据大小总和 todo 这个计算方式，我有些不理解
    q->size += pkt1.pkt->size + sizeof(pkt1);
    q->duration += pkt1.pkt->duration;
    /* XXX: should duplicate packet data in DV case */
    // 发出信号，表明当前队列中有数据了，通知等待中的读线程可以取数据了
    SDL_CondSignal(q->cond);
    return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    AVPacket *pkt1;
    int      ret;

    // 申请 avpacket 并做相关check
    pkt1 = av_packet_alloc();
    if (!pkt1) {
        av_packet_unref(pkt);
        return -1;
    }
    // 将pkt 的值 转移到pkt1 中
    av_packet_move_ref(pkt1, pkt);

    SDL_LockMutex(q->mutex);
    // 把 pkt1 放进 PacketQueue
    ret = packet_queue_put_private(q, pkt1);
    SDL_UnlockMutex(q->mutex);

    // 哇哦，，sorry 添加队列的失败了
    if (ret < 0)
        av_packet_free(&pkt1);

    return ret;
}

/**
 * NOTE：视频，音频，字符 会调用这个函数；
 * 放入空包意味着流的结束，一般在媒体数据读取完成的时候放入空包；
 * @param q
 * @param pkt
 * @param stream_index
 * @return
 */
static int packet_queue_put_nullpacket(PacketQueue *q, AVPacket *pkt, int stream_index) {
    // todo 看看pkt->data 是不是 为空
    if (pkt->data == NULL) {
        av_log(NULL, AV_LOG_INFO, " %d 流 放进一个空 packet", pkt->stream_index);
    }
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}


/**
 * 初始化: packet queue handling
 *
 * @param q
 * @return
 */
static int packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->pkt_list = av_fifo_alloc(sizeof(MyAVPacketList));
    if (!q->pkt_list)
        return AVERROR(ENOMEM);
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}


/**
 * NOTE: 在媒体流数据读取完后，此时编码器还缓存有数据，把空节点（空的AVPacket）添加到链表，
 * 解码线程，把空的AVPacket放入解码器，此时解码器就会认为是流结束，
 * 会把缓存的所有frame都冲刷出来。
 * ----------------------------------------------------------------------
 * packet_queue_flush: 清除队列内所有的节点，包括节点对应的AVPacket，主要用于退出播放、seek播放。
 * @param q
 */
static void packet_queue_flush(PacketQueue *q) {
    MyAVPacketList pkt1;

    SDL_LockMutex(q->mutex);
    while (av_fifo_size(q->pkt_list) >= sizeof(pkt1)) {
        // 往 AVFifoBuffer 里面读内存数据：把 （buffer）q->pkt_list 的数据 读到 pkt1 中
        // 实际，就是借用 AVPacket 释放buffer数据，这个十分巧妙，借刀杀人.....
        av_fifo_generic_read(q->pkt_list, &pkt1, sizeof(pkt1), NULL);
        // free &pkt1.pkt ： AVPacket
        av_packet_free(&pkt1.pkt);
    }
    // 队列数据擦除
    q->nb_packets = 0;
    q->size       = 0;
    q->duration   = 0;

    // todo 让PacketQueue 和 MyAVPacketList 里的serial 出现不一致，便于做丢帧操作。
    q->serial++;
    printf("packet_queue_flush() :after  (q->serial++) ,serial is %d\n: ",q->serial);
    SDL_UnlockMutex(q->mutex);
}


/**
 * 销毁
 * @param q
 */
static void packet_queue_destroy(PacketQueue *q) {
    // 擦除度列数据
    packet_queue_flush(q);
    av_fifo_freep(&q->pkt_list);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

/**
 * 终止队列
 * @param q
 */
static void packet_queue_abort(PacketQueue *q) {
    SDL_LockMutex(q->mutex);

    // 请求退出
    q->abort_request = 1;
    // 释放一个信号
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

/**
 * 启用
 * @param q
 */
static void packet_queue_start(PacketQueue *q) {
    // 线程安全
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;

    q->serial++;
    printf("packet_queue_start() : after (q->serial++), serial is : %d\n",q->serial);
    SDL_UnlockMutex(q->mutex);
}

/**
 *  获取一个节点: 重点看 *serial 是怎么被 赋值的？


 * return < 0 if aborted, 0 if no packet and > 0 if packet.
 * @param q
 * @param pkt
 * @param block  调⽤者是否需要 在没节点可取的情况下 阻塞等待
 * @param serial  输出参数，即MyAVPacketList.serial
 * @return
 */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial) {
    puts("packet_queue_get(): ");
    MyAVPacketList pkt1;
    int            ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (q->abort_request) {// abort_request 是 1 的话，就表示退出
            ret = -1;
            // 退出loop
            break;
        }

        if (av_fifo_size(q->pkt_list) >= sizeof(pkt1)) {
            av_fifo_generic_read(q->pkt_list, &pkt1, sizeof(pkt1), NULL);

            // 节点数 -1
            q->nb_packets--;// AVPacket 给了pkt1 ，number 要 减去1；
            printf("      packet_queue_get() : after (q->nb_packets--) , nb_packets is : %d\n",q->nb_packets);
            // cache 大小，扣除一个节点
            q->size -= pkt1.pkt->size + sizeof(pkt1);
            // 总时长扣除一个节点的时长
            q->duration -= pkt1.pkt->duration;

            // pkt 有值了，pkt1 的数据，被擦除了 todo PS： 又TM move 回去了?
            av_packet_move_ref(pkt, pkt1.pkt);
            if (serial) {
                // 在此给下一个节点赋值
                *serial = pkt1.serial;
            }
            av_packet_free(&pkt1.pkt);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            // 这⾥没有break，for循环的另⼀个作⽤是在条件变量满⾜后重复上述代码取出节点
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

/**
 *
 * @param d
 * @param avctx
 * @param queue
 * @param empty_queue_cond :  实际上就是 continue_read_thread，只是换了个名字。
 * @return
 */
static int decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond) {
    puts("decoder_init() ");
    printf("    decoder_init: %s : empty_queue_cond pointer address is : %p\n", d->decode_name, empty_queue_cond);
    memset(d, 0, sizeof(Decoder));
    d->pkt = av_packet_alloc();

    if (!d->pkt) {
        return AVERROR(ENOMEM);
    }

    d->avctx            = avctx;
    d->queue            = queue;
    d->empty_queue_cond = empty_queue_cond;
    d->start_pts        = AV_NOPTS_VALUE;
    // init 时，序号为-1；
    d->pkt_serial       = -1;
    return 0;
}

/**
 * 音视频和字幕，都调用这个函数， 内部调用 avcodec_send_packet
 * @param d
 * @param frame
 * @param sub
 * @return  case 1 ： 返回 1，获取到 AVFrame
 *          case 2 ： 返回 0 ，获取不到 AVFrame ，0 代表已经解码完MP4的所有AVPacket。
 *                    这种情况一般是 ffplay 播放完了整个 MP4 文件，窗口画面停在最后一帧。
 *                    但是由于你可以按 C 键重新循环播放，所以即便返回 0 也不能退出 audio_thread 线程。

 *          case 2 ： 返回 -1，代表 PacketQueue 队列关闭了（abort_request）。
 *                     返回 -1 会导致 audio_thread() 函数用 goto the_end 跳出 do{} while{} 循环，跳出循环之后，
 *                     audio_thread 线程就会自己结束了。返回 -1 通常是因为关闭了 ffplay 播放器。
 */
static int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    printf("decoder_decode_frame() By : %s\n", SDL_GetThreadName(d->decoder_tid));
    int ret = AVERROR(EAGAIN);

    for (;;) {
        //  队列serial 和 解码器serial 一致,可以接受 解码器处理出来的 AVFrame
        if (d->queue->serial == d->pkt_serial) {
            do {
                if (d->queue->abort_request) {
                    return -1;
                }

                switch (d->avctx->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            if (decoder_reorder_pts == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!decoder_reorder_pts) {
                                frame->pts = frame->pkt_dts;
                            }
                        }
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            AVRational tb = (AVRational) {1, frame->sample_rate};
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
                            else if (d->next_pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                //  next_pts 的计算规则就是上一帧的 pts 加上他的样本数（也就是播放多久）
                                d->next_pts    = frame->pts + frame->nb_samples;
                                d->next_pts_tb = tb;
                            }
                        }
                        break;
                }

                if (ret == AVERROR_EOF) {
                    puts("      decoder_decode_frame() : 解码AVPacket 结束，");
                    d->finished = d->pkt_serial;
                    avcodec_flush_buffers(d->avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }


        // ======== 线程通信 start======
        do {
            if (d->queue->nb_packets == 0) {
                puts("      decoder_decode_frame() : 给 empty_queue_cond 发一个信号，我这边 没有 AVPacket 了");
                SDL_CondSignal(d->empty_queue_cond);
            }

            if (d->packet_pending) {
                d->packet_pending = 0;
            } else {
                int old_serial = d->pkt_serial;
                if (packet_queue_get(d->queue, d->pkt, 1, &d->pkt_serial) < 0)
                    return -1;
                if (old_serial != d->pkt_serial) {
                    // seek 了，所以,清空内部缓存的帧数据
                    avcodec_flush_buffers(d->avctx);
                    d->finished    = 0;
                    d->next_pts    = d->start_pts;
                    d->next_pts_tb = d->start_pts_tb;
                }
            }
            if (d->queue->serial == d->pkt_serial) {
                puts("      decoder_decode_frame() : 队列serial 和 解码器serial 一致了，中断while ，着手解码 AVPacket了。");
                break;
            }

            av_packet_unref(d->pkt);
        } while (1);
        // ======== 线程通信 end======

        // 循环解码
        if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            puts("      decoder_decode_frame() : 解码字幕.");

            int got_frame = 0;
            ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, d->pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            } else {
                if (got_frame && !d->pkt->data) {
                    d->packet_pending = 1;
                }
                ret = got_frame ? 0 : (d->pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(d->pkt);
        } else {
            puts("      decoder_decode_frame() : 解码 音频和视频.");
            if (avcodec_send_packet(d->avctx, d->pkt) == AVERROR(EAGAIN)) {
                av_log(d->avctx, AV_LOG_ERROR,
                       "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");

                // 发送 pkt 失败，下次再次发送....
                d->packet_pending = 1;
            } else {
                av_packet_unref(d->pkt);
            }
        }
    }
}

static void decoder_destroy(Decoder *d) {
    av_packet_free(&d->pkt);
    avcodec_free_context(&d->avctx);
}

/**
 * // 释放对 vp->frame中的数据缓冲区AVBuffer的引⽤
 * @param vp
 */
static void frame_queue_unref_item(Frame *vp) {
    // 解去 frame 的引用
    av_frame_unref(vp->frame);
    avsubtitle_free(&vp->sub);
}

/**
 * 初始化队列
 * @param f
 * @param pktq
 * @param max_size  队列大小，视频是3, 音频是9；
 * @param keep_last
 * @return
 */
static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last) {
    puts("frame_queue_init()");
    int i;
    memset(f, 0, sizeof(FrameQueue));
    // 创建 互斥锁
    if (!(f->mutex = SDL_CreateMutex())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    // 创建 互斥条件
    if (!(f->cond  = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    f->pktq        = pktq;
    //队列大小
    f->max_size    = FFMIN(max_size, FRAME_QUEUE_SIZE);
    printf("    frame_queue_init() Line number ：%d,\n   FrameQueue size is : %d\n ",__LINE__,f->max_size);

    // 将int取值的keep_last 转换 为bool取值（0或1）todo 神奇的代码.....
    f->keep_last = !!keep_last;

    for (i = 0; i < f->max_size; i++)
        // 为每一个节点的 AVFrame 分配内存，并不是缓存区的内存
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

/**
 * 销毁队列
 * @param f
 */
static void frame_queue_destory(FrameQueue *f) {
    puts("销毁队列");
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        // 俄罗斯套娃，，， ，内部肯定是free avFrame
        frame_queue_unref_item(vp);
        // free avFrame.
        av_frame_free(&vp->frame);
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

/**
 *
 * @param f
 */
static void frame_queue_signal(FrameQueue *f) {
    SDL_LockMutex(f->mutex);
    printf("    frame_queue_signal() : 发送唤醒信号 f->cond pointer add is : %p\n",f->cond);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/**
 * 获取当前Frame，调⽤之前先调⽤frame_queue_nb_remaining确保有frame可读
 * @param f
 * @return
 */
static Frame *frame_queue_peek(FrameQueue *f) {
    // 模运算是是TM怕 数组越界
    // 注意下标计算： f->rindex + f->rindex_shown
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

/**
 *：获取下⼀Frame
 * NOTE:获取当前帧的下一帧，此时要确保queue里面至少有2个frame
 * @param f
 * @return
 */
static Frame *frame_queue_peek_next(FrameQueue *f) {
    puts("frame_queue_peek_next() : 获取下⼀Frame\n");
    //  获取当前帧的下一帧，此时要确保queue里面至少有2个frame
    // 靠，，，，这TM 不是队列，，，是直接操作数组，，好不好？
    //  // 注意下标计算： f->rindex + f->rindex_shown+1
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

/**
 * 获取上⼀Frame： 注意，这里的 rindex.....
 * NOTE： 队列，是先进先出的。
 * @param f
 * @return
 */
static Frame *frame_queue_peek_last(FrameQueue *f) {
    puts("frame_queue_peek_last()");

    return &f->queue[f->rindex];
}

/**
 * 从 FrameQueue 里面取一个可以写的 Frame 出来 ，可以：以阻塞或⾮阻塞⽅式进⾏,
 * NOTE:向队列尾部申请一个可写的帧空间，若队列已满无空间可写，
 * 则等待（由SDL_cond *cond控制，由frame_queue_next或frame_queue_signal触发唤 醒）
 * @param f
 * @return
 */
static Frame *frame_queue_peek_writable(FrameQueue *f) {
    puts("frame_queue_peek_writable()");
    /* wait until we have space to put a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size && // 当前总帧数 >=可存储最大帧数
           !f->pktq->abort_request) // 用户没有退出请求
    {


        printf("    frame_queue_peek_writable()-----------------------------------------------------------------------------------------------------------------| \n");
        printf("    frame_queue_peek_writable() : SDL_CondWait 一会儿，FrameQueue 满了，需要被消耗，在线等，挺着急的.\n");
        printf("    frame_queue_peek_writable() : f->cond pointer add is : %p\n",f->cond);
        printf("    frame_queue_peek_writable() : f->size : %d ,f->max_size : %d \n", f->size, f->max_size);
        printf("    frame_queue_peek_writable()-----------------------------------------------------------------------------------------------------------------| \n\n");

        // wo TM 等....等FrameQueue 的 queue 被消耗一些....
        SDL_CondWait(f->cond, f->mutex);
        printf("    frame_queue_peek_writable()-----------------------------------------------------------------------------------------------------------------| \n");
        printf("    frame_queue_peek_writable() : SDL_CondWait  收到了唤醒信号，FrameQueue 有空间了。\n");
        printf("    frame_queue_peek_writable() : f->cond pointer add is : %p\n",f->cond);
        printf("    frame_queue_peek_writable() : f->size : %d ,f->max_size : %d \n", f->size, f->max_size);
        printf("    frame_queue_peek_writable()-----------------------------------------------------------------------------------------------------------------| \n\n");

    }
    SDL_UnlockMutex(f->mutex);



    // 用户退出了，直接返回NULL
    if (f->pktq->abort_request) {
        return NULL;
    }
    // 从队列中返回一个可写的Frame
    // 注意： 是队尾...是队尾...是队尾
    printf("    frame_queue_peek_writable() : windex is : %d\n",f->windex);
    return &f->queue[f->windex];
}

/**
 * 获取⼀个可读Frame，可以以阻塞或⾮阻塞⽅式进⾏
 * NOTE:从队列头部读取一帧，只读取不删除，若无帧可读则等待
 * @param f
 * @return
 */
static Frame *frame_queue_peek_readable(FrameQueue *f) {
    /* wait until we have a readable a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
           !f->pktq->abort_request) {
        printf("    frame_queue_peek_readable()---------------------------------------------------------------------------------| \n");
        printf("    frame_queue_peek_readable() : SDL_CondWait 一会儿，FrameQueue 没数据了，需要被添加，在线等，挺着急的.\n");
        printf("    frame_queue_peek_readable() : f->cond pointer add is : %p",f->cond);
        printf("    frame_queue_peek_readable() : f->size : %d ,f->rindex_shown : %d \n", f->size, f->rindex_shown);
        printf("    frame_queue_peek_readable()---------------------------------------------------------------------------------| \n\n");
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    // 注意这里的下标计算方式：f->rindex + f->rindex_shown
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

/**
 * 更新写索引，此时Frame才真正⼊队列，队列节点Frame个数加1
 * 向队列尾部压入一帧，只更新计数 与 写指针，
 * 因此条用此函数前，应将帧数据写入队列相应位置，
 *
 * SDL_CondSignal唤醒读frame_queue_peek_readable
 *
 * @param f
 */
static void frame_queue_push(FrameQueue *f) {
    puts("frame_queue_push(): ");
    if (++f->windex == f->max_size) {
        // 世界是一个圆，我又回到了最初的起点....
        f->windex = 0;
    }
    SDL_LockMutex(f->mutex);
    // 当前总帧数 + 1 todo 需要看看 这个size 在哪里被使用
    f->size++;
    printf("    frame_queue_push() ========================================================\n");
    printf("    frame_queue_push() | 发送唤醒信号 f->cond pointer add is : %p\n",f->cond);
    printf("    frame_queue_push() | f->size : %d ,f->max_size : %d \n", f->size, f->max_size);
    printf("    frame_queue_push() ========================================================\n\n");
    SDL_CondSignal(f->cond);//当frame_queue_peek_readable在等待时，可唤醒
    SDL_UnlockMutex(f->mutex);
}

/**
 * 读取当前准备播放的帧的下一个帧
 * 更新读索引，此时Frame才真正出队列，队列节点Frame个数减1
 * case 1: 当启用keep_last时，如果rindex_shown为0,则将其设置为1，并返回；
 *         此时并不会更新读索引，也就是说keep_last机制实质上也会占用着队列的大小，
 * case 2: 当调用frame_queue_nb_remaining获取size时并不能将其计算入size；
 *          释放Frame对应的数据，
 *          但不释放Frame节点本身；
 *          更新读索引；
 *          释放唤醒信号，
 *          以环形正在等待写入的线程。
 * @param f
 */
static void frame_queue_next(FrameQueue *f) {
    puts("frame_queue_next() : 更新读索引");
    // case 1:
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    // 删除frame
    frame_queue_unref_item(&f->queue[f->rindex]);
    printf("    frame_queue_next() rindex   : %d\n",f->rindex);
    printf("    frame_queue_next() max_size : %d\n",f->max_size);
    // case 2:
    // 世界还是一个圆，可读index回到当初变成0；
    // FrameQueue 是一个数组，数组的下边是从0 开始的，所以最大的下标比
    if (++f->rindex == f->max_size)
    {
        f->rindex = 0;
    }
    SDL_LockMutex(f->mutex);
    f->size--;// 当前总帧数 -1; PS : frame 出列了，所以总帧数要减 1 啊....
    SDL_CondSignal(f->cond);
    printf("  frame_queue_next() : rindex  is : %d\n",f->rindex);
    SDL_UnlockMutex(f->mutex);
}

/*
 * 获取队列剩余大小
 * return the number of undisplayed frames in the queue
 * */
static int frame_queue_nb_remaining(FrameQueue *f) {
    return f->size - f->rindex_shown;
}

/*
 * 获取最近播放Frame对应数据在媒体⽂件的位置，主要在seek时使⽤
 * 获取当前播放到文件的那个位置，位置是内存数据的位置。例如 100M 的mp4，播放到了 50M。
 *
 * return last shown position */
static int64_t frame_queue_last_pos(FrameQueue *f) {
    puts("frame_queue_last_pos()");
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}

static void decoder_abort(Decoder *d, FrameQueue *fq) {
    puts("decoder_abort(): ");
    packet_queue_abort(d->queue);
    frame_queue_signal(fq);
    SDL_WaitThread(d->decoder_tid, NULL);
    d->decoder_tid = NULL;
    packet_queue_flush(d->queue);
}

static inline void fill_rectangle(int x, int y, int w, int h) {
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (w && h)
        SDL_RenderFillRect(renderer, &rect);
}

static int
realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode,
                int init_texture) {
    Uint32 format;
    int    access, w, h;
    if (!*texture || SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h ||
        new_format != format) {
        void *pixels;
        int  pitch;
        if (*texture)
            SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        if (init_texture) {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
        av_log(NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n", new_width, new_height,
               SDL_GetPixelFormatName(new_format));
    }
    return 0;
}

static void calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar) {
    AVRational aspect_ratio = pic_sar;
    int64_t    width, height, x, y;

    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
        aspect_ratio = av_make_q(1, 1);

    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width  = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > scr_width) {
        width  = scr_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }
    x      = (scr_width - width) / 2;
    y      = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop + y;
    rect->w = FFMAX((int) width, 1);
    rect->h = FFMAX((int) height, 1);
}

static void get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode) {
    int i;
    *sdl_blendmode     = SDL_BLENDMODE_NONE;
    *sdl_pix_fmt       = SDL_PIXELFORMAT_UNKNOWN;
    if (format == AV_PIX_FMT_RGB32 ||
        format == AV_PIX_FMT_RGB32_1 ||
        format == AV_PIX_FMT_BGR32 ||
        format == AV_PIX_FMT_BGR32_1)
        *sdl_blendmode = SDL_BLENDMODE_BLEND;
    for (i = 0; i < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; i++) {
        if (format == sdl_texture_format_map[i].format) {
            *sdl_pix_fmt = sdl_texture_format_map[i].texture_fmt;
            return;
        }
    }
}

static int upload_texture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx) {
    int           ret = 0;
    Uint32        sdl_pix_fmt;
    SDL_BlendMode sdl_blendmode;
    get_sdl_pix_fmt_and_blendmode(frame->format, &sdl_pix_fmt, &sdl_blendmode);
    if (realloc_texture(tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt,
                        frame->width, frame->height, sdl_blendmode, 0) < 0)
        return -1;
    switch (sdl_pix_fmt) {
        case SDL_PIXELFORMAT_UNKNOWN:
            /* This should only happen if we are not using avfilter... */
            *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
                                                    frame->width, frame->height, frame->format, frame->width,
                                                    frame->height,
                                                    AV_PIX_FMT_BGRA, sws_flags, NULL, NULL, NULL);
            if (*img_convert_ctx != NULL) {
                uint8_t *pixels[4];
                int     pitch[4];
                if (!SDL_LockTexture(*tex, NULL, (void **) pixels, pitch)) {
                    sws_scale(*img_convert_ctx, (const uint8_t *const *) frame->data, frame->linesize,
                              0, frame->height, pixels, pitch);
                    SDL_UnlockTexture(*tex);
                }
            } else {
                av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                ret = -1;
            }
            break;
        case SDL_PIXELFORMAT_IYUV:
            if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
                                           frame->data[1], frame->linesize[1],
                                           frame->data[2], frame->linesize[2]);
            } else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1),
                                           -frame->linesize[0],
                                           frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1),
                                           -frame->linesize[1],
                                           frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1),
                                           -frame->linesize[2]);
            } else {
                av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
                return -1;
            }
            break;
        default:
            if (frame->linesize[0] < 0) {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1),
                                        -frame->linesize[0]);
            } else {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
            }
            break;
    }
    return ret;
}

static void set_sdl_yuv_conversion_mode(AVFrame *frame) {
#if SDL_VERSION_ATLEAST(2, 0, 8)
    SDL_YUV_CONVERSION_MODE mode = SDL_YUV_CONVERSION_AUTOMATIC;
    if (frame && (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUYV422 ||
                  frame->format == AV_PIX_FMT_UYVY422)) {
        if (frame->color_range == AVCOL_RANGE_JPEG)
            mode = SDL_YUV_CONVERSION_JPEG;
        else if (frame->colorspace == AVCOL_SPC_BT709)
            mode = SDL_YUV_CONVERSION_BT709;
        else if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M ||
                 frame->colorspace == AVCOL_SPC_SMPTE240M)
            mode = SDL_YUV_CONVERSION_BT601;
    }
    SDL_SetYUVConversionMode(mode);
#endif
}

static void video_image_display(VideoState *is) {
    Frame    *vp;
    Frame    *sp = NULL;
    SDL_Rect rect;

    vp = frame_queue_peek_last(&is->pictq);
    if (is->subtitle_st) {
        if (frame_queue_nb_remaining(&is->subpq) > 0) {
            sp = frame_queue_peek(&is->subpq);

            if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
                if (!sp->uploaded) {
                    uint8_t *pixels[4];
                    int     pitch[4];
                    int     i;
                    if (!sp->width || !sp->height) {
                        sp->width  = vp->width;
                        sp->height = vp->height;
                    }
                    if (realloc_texture(&is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height,
                                        SDL_BLENDMODE_BLEND, 1) < 0)
                        return;

                    for (i       = 0; i < sp->sub.num_rects; i++) {
                        AVSubtitleRect *sub_rect = sp->sub.rects[i];

                        sub_rect->x = av_clip(sub_rect->x, 0, sp->width);
                        sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
                        sub_rect->w = av_clip(sub_rect->w, 0, sp->width - sub_rect->x);
                        sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

                        is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
                                                                   sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                                                                   sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                                                                   0, NULL, NULL, NULL);
                        if (!is->sub_convert_ctx) {
                            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                            return;
                        }
                        if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *) sub_rect, (void **) pixels, pitch)) {
                            sws_scale(is->sub_convert_ctx, (const uint8_t *const *) sub_rect->data, sub_rect->linesize,
                                      0, sub_rect->h, pixels, pitch);
                            SDL_UnlockTexture(is->sub_texture);
                        }
                    }
                    sp->uploaded = 1;
                }
            } else
                sp = NULL;
        }
    }

    calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);

    if (!vp->uploaded) {
        if (upload_texture(&is->vid_texture, vp->frame, &is->img_convert_ctx) < 0)
            return;
        vp->uploaded = 1;
        vp->flip_v   = vp->frame->linesize[0] < 0;
    }

    set_sdl_yuv_conversion_mode(vp->frame);
    SDL_RenderCopyEx(renderer, is->vid_texture, NULL, &rect, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : 0);
    set_sdl_yuv_conversion_mode(NULL);
    if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER
        SDL_RenderCopy(renderer, is->sub_texture, NULL, &rect);
#else
        int i;
        double xratio = (double)rect.w / (double)sp->width;
        double yratio = (double)rect.h / (double)sp->height;
        for (i = 0; i < sp->sub.num_rects; i++) {
            SDL_Rect *sub_rect = (SDL_Rect*)sp->sub.rects[i];
            SDL_Rect target = {.x = rect.x + sub_rect->x * xratio,
                               .y = rect.y + sub_rect->y * yratio,
                               .w = sub_rect->w * xratio,
                               .h = sub_rect->h * yratio};
            SDL_RenderCopy(renderer, is->sub_texture, sub_rect, &target);
        }
#endif
    }
}

static inline int compute_mod(int a, int b) {
    return a < 0 ? a % b + b : a % b;
}

static void video_audio_display(VideoState *s) {
    int     i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
    int     ch, channels, h, h2;
    int64_t time_diff;
    int     rdft_bits, nb_freq;

    for (rdft_bits = 1; (1 << rdft_bits) < 2 * s->height; rdft_bits++);
    nb_freq        = 1 << (rdft_bits - 1);

    /* compute display index : center on currently output samples */
    channels            = s->audio_tgt.channels;
    nb_display_channels = channels;
    if (!s->paused) {
        int data_used = s->show_mode == SHOW_MODE_WAVES ? s->width : (2 * nb_freq);
        n             = 2 * channels;
        delay         = s->audio_write_buf_size;
        delay /= n;

        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (audio_callback_time) {
            time_diff = av_gettime_relative() - audio_callback_time;
            delay -= (time_diff * s->audio_tgt.freq) / 1000000;
        }

        delay += 2 * data_used;
        if (delay < data_used)
            delay     = data_used;

        i_start = x = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
        if (s->show_mode == SHOW_MODE_WAVES) {
            h      = INT_MIN;
            for (i = 0; i < 1000; i += channels) {
                int idx   = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
                int a     = s->sample_array[idx];
                int b     = s->sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
                int c     = s->sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
                int d     = s->sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
                int score = a - d;
                if (h < score && (b ^ c) < 0) {
                    h       = score;
                    i_start = idx;
                }
            }
        }

        s->last_i_start = i_start;
    } else {
        i_start = s->last_i_start;
    }

    if (s->show_mode == SHOW_MODE_WAVES) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        /* total height for one channel */
        h       = s->height / nb_display_channels;
        /* graph height / 2 */
        h2      = (h * 9) / 20;
        for (ch = 0; ch < nb_display_channels; ch++) {
            i      = i_start + ch;
            y1     = s->ytop + ch * h + (h / 2); /* position of center line */
            for (x = 0; x < s->width; x++) {
                y = (s->sample_array[i] * h2) >> 15;
                if (y < 0) {
                    y  = -y;
                    ys = y1 - y;
                } else {
                    ys = y1;
                }
                fill_rectangle(s->xleft + x, ys, 1, y);
                i += channels;
                if (i >= SAMPLE_ARRAY_SIZE)
                    i -= SAMPLE_ARRAY_SIZE;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);

        for (ch = 1; ch < nb_display_channels; ch++) {
            y = s->ytop + ch * h;
            fill_rectangle(s->xleft, y, s->width, 1);
        }
    } else {
        if (realloc_texture(&s->vis_texture, SDL_PIXELFORMAT_ARGB8888, s->width, s->height, SDL_BLENDMODE_NONE, 1) < 0)
            return;

        if (s->xpos >= s->width)
            s->xpos = 0;
        nb_display_channels = FFMIN(nb_display_channels, 2);
        if (rdft_bits != s->rdft_bits) {
            av_rdft_end(s->rdft);
            av_free(s->rdft_data);
            s->rdft      = av_rdft_init(rdft_bits, DFT_R2C);
            s->rdft_bits = rdft_bits;
            s->rdft_data = av_malloc_array(nb_freq, 4 * sizeof(*s->rdft_data));
        }
        if (!s->rdft || !s->rdft_data) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
            s->show_mode = SHOW_MODE_WAVES;
        } else {
            FFTSample *data[2];
            SDL_Rect  rect = {.x = s->xpos, .y = 0, .w = 1, .h = s->height};
            uint32_t  *pixels;
            int       pitch;
            for (ch = 0; ch < nb_display_channels; ch++) {
                data[ch] = s->rdft_data + 2 * nb_freq * ch;
                i      = i_start + ch;
                for (x = 0; x < 2 * nb_freq; x++) {
                    double w = (x - nb_freq) * (1.0 / nb_freq);
                    data[ch][x] = s->sample_array[i] * (1.0 - w * w);
                    i += channels;
                    if (i >= SAMPLE_ARRAY_SIZE)
                        i -= SAMPLE_ARRAY_SIZE;
                }
                av_rdft_calc(s->rdft, data[ch]);
            }
            /* Least efficient way to do this, we should of course
             * directly access it but it is more than fast enough. */
            if (!SDL_LockTexture(s->vis_texture, &rect, (void **) &pixels, &pitch)) {
                pitch >>= 2;
                pixels += pitch * s->height;
                for (y = 0; y < s->height; y++) {
                    double w = 1 / sqrt(nb_freq);
                    int    a = sqrt(w * sqrt(data[0][2 * y + 0] * data[0][2 * y + 0] +
                                             data[0][2 * y + 1] * data[0][2 * y + 1]));
                    int    b = (nb_display_channels == 2) ? sqrt(w * hypot(data[1][2 * y + 0], data[1][2 * y + 1]))
                                                          : a;
                    a = FFMIN(a, 255);
                    b = FFMIN(b, 255);
                    pixels -= pitch;
                    *pixels = (a << 16) + (b << 8) + ((a + b) >> 1);
                }
                SDL_UnlockTexture(s->vis_texture);
            }
            SDL_RenderCopy(renderer, s->vis_texture, NULL, NULL);
        }
        if (!s->paused)
            s->xpos++;
    }
}

static void stream_component_close(VideoState *is, int stream_index) {
    puts("stream_component_close() ");
    AVFormatContext   *ic = is->ic;
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            decoder_abort(&is->auddec, &is->sampq);
            SDL_CloseAudioDevice(audio_dev);
            decoder_destroy(&is->auddec);
            swr_free(&is->swr_ctx);
            av_freep(&is->audio_buf1);
            is->audio_buf1_size = 0;
            is->audio_buf       = NULL;

            if (is->rdft) {
                av_rdft_end(is->rdft);
                av_freep(&is->rdft_data);
                is->rdft      = NULL;
                is->rdft_bits = 0;
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            decoder_abort(&is->viddec, &is->pictq);
            decoder_destroy(&is->viddec);
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            decoder_abort(&is->subdec, &is->subpq);
            decoder_destroy(&is->subdec);
            break;
        default:
            break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            is->audio_st     = NULL;
            is->audio_stream = -1;
            break;
        case AVMEDIA_TYPE_VIDEO:
            is->video_st     = NULL;
            is->video_stream = -1;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            is->subtitle_st     = NULL;
            is->subtitle_stream = -1;
            break;
        default:
            break;
    }
}

static void stream_close(VideoState *is) {
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    SDL_WaitThread(is->read_tid, NULL);

    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(is, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(is, is->video_stream);
    if (is->subtitle_stream >= 0)
        stream_component_close(is, is->subtitle_stream);

    avformat_close_input(&is->ic);

    packet_queue_destroy(&is->videoq);
    packet_queue_destroy(&is->audioq);
    packet_queue_destroy(&is->subtitleq);

    /* free all pictures */
    frame_queue_destory(&is->pictq);
    frame_queue_destory(&is->sampq);
    frame_queue_destory(&is->subpq);
    SDL_DestroyCond(is->continue_read_thread);
    sws_freeContext(is->img_convert_ctx);
    sws_freeContext(is->sub_convert_ctx);
    av_free(is->filename);
    if (is->vis_texture)
        SDL_DestroyTexture(is->vis_texture);
    if (is->vid_texture)
        SDL_DestroyTexture(is->vid_texture);
    if (is->sub_texture)
        SDL_DestroyTexture(is->sub_texture);
    av_free(is);
}

static void do_exit(VideoState *is) {
    if (is) {
        stream_close(is);
    }
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    uninit_opts();
#if CONFIG_AVFILTER
    av_freep(&vfilters_list);
#endif
    avformat_network_deinit();
    if (show_status)
        printf("\n");
    SDL_Quit();
    av_log(NULL, AV_LOG_QUIET, "%s", "");
    exit(0);
}

static void sigterm_handler(int sig) {
    exit(123);
}

static void set_default_window_size(int width, int height, AVRational sar) {
    SDL_Rect rect;
    int      max_width  = screen_width ? screen_width : INT_MAX;
    int      max_height = screen_height ? screen_height : INT_MAX;
    if (max_width == INT_MAX && max_height == INT_MAX) {
        max_height = height;
    }

    calculate_display_rect(&rect, 0, 0, max_width, max_height, width, height, sar);
    default_width  = rect.w;
    default_height = rect.h;
}

static int video_open(VideoState *is) {
    int w, h;

    w = screen_width ? screen_width : default_width;
    h = screen_height ? screen_height : default_height;

    if (!window_title)
        window_title = input_filename;
    SDL_SetWindowTitle(window, window_title);

    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, screen_left, screen_top);
    if (is_full_screen)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_ShowWindow(window);

    is->width  = w;
    is->height = h;

    return 0;
}

/* display the current picture, if any */
static void video_display(VideoState *is) {
    if (!is->width)
        video_open(is);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (is->audio_st && is->show_mode != SHOW_MODE_VIDEO)
        video_audio_display(is);
    else if (is->video_st)
        video_image_display(is);
    SDL_RenderPresent(renderer);
}

static double get_clock(Clock *c) {
    puts("get_clock()");
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        // av_gettime_relative() 获取当前的系统时间
        double time = av_gettime_relative() / 1000000.0;
        printf("    get_clock() : OS time is : %f\n",time);

        //  c->speed 默认是 1；
        // 视频流当前的播放时刻 = 当前帧的 pts - 之前记录的系统时间 + 当前的系统时间
        // 视频流当前的播放时刻 = 当前帧的 pts + 当前的系统时间    - 之前记录的系统时间
        // 视频流当前的播放时刻 = 当前帧的 pts + 消逝的时间

        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

/**
 * 设置时钟相关参数。
 * @param c      Clock: 音频时钟，或者 视频时钟，记录 current time ,这个多媒体流播放到哪里了.
 * @param pts
 * @param serial
 * @param time
 */
static void set_clock_at(Clock *c, double pts, int serial, double time) {
    c->pts          = pts;
    c->last_updated = time;
    c->pts_drift    = c->pts - time;
    c->serial       = serial;
}

static void set_clock(Clock *c, double pts, int serial) {
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

static void set_clock_speed(Clock *c, double speed) {
    set_clock(c, get_clock(c), c->serial);
    c->speed = speed;
}

static void init_clock(Clock *c, int *queue_serial) {
    c->speed        = 1.0;
    c->paused       = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

static void sync_clock_to_slave(Clock *c, Clock *slave) {
    double clock       = get_clock(c);
    double slave_clock = get_clock(slave);
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}

static int get_master_sync_type(VideoState *is) {
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
static double get_master_clock(VideoState *is) {
    double val;
    switch (get_master_sync_type(is)) {
        case AV_SYNC_VIDEO_MASTER:
            val = get_clock(&is->vidclk);
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = get_clock(&is->audclk);
            break;
        default:
            val = get_clock(&is->extclk);
            break;
    }
    printf("get_master_clock() \n   the current master clock value  : %f\n", val);
    return val;
}

static void check_external_clock_speed(VideoState *is) {
    if (is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
        is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) {
        set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    } else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
               (is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
        set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
    } else {
        double speed = is->extclk.speed;
        if (speed != 1.0)
            set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes) {
    puts("stream_seek()");
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes) {
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        }
        is->seek_req = 1;
        puts("      stream_seek(): SDL_CondSignal: 发个信号，用户有 seek 操作.");
        SDL_CondSignal(is->continue_read_thread);
    }
}

/* pause or resume the video */
static void stream_toggle_pause(VideoState *is) {
    if (is->paused) {
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
    }
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

static void toggle_pause(VideoState *is) {
    stream_toggle_pause(is);
    is->step = 0;
}

static void toggle_mute(VideoState *is) {
    is->muted = !is->muted;
}

static void update_volume(VideoState *is, int sign, double step) {
    double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double) SDL_MIX_MAXVOLUME) / log(10))
                                           : -1000.0;
    int    new_volume   = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0,
                               SDL_MIX_MAXVOLUME);
}

static void step_to_next_frame(VideoState *is) {
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause(is);
    is->step = 1;
}

/**
 *
 * @param delay : 代表当前帧本来，本来需要显示多长时间。当前帧是 指 窗口正在显示的帧。
 * @param is    : VideoState
 * @return      : 还是返回一个delay，当前帧实际，实际应该显示多长时间,
 */
static double compute_target_delay(double delay, VideoState *is) {
    puts("compute_target_delay() ") ;
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = get_clock(&is->vidclk) - get_master_clock(is);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know if it is the best guess */
        // 同步阈值
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        printf("    compute_target_delay() # sync_threshold : %f\n",sync_threshold);
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            // case 1: diff 是负数的时候，代表视频比音频慢了，通常会将 delay 置为 0，
            if (diff <= -sync_threshold) {
                delay = FFMAX(0, delay + diff);
            }
            // case 2: diff 大于阈值，并且，当前Frame需要显示的时间大于 0.1s;
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD) {
                delay = delay + diff;
            }
            // case 3: diff 是正数的时候，代表视频比音频快了，当超过阈值的时候，就会把 delay * 2
            else if (diff >= sync_threshold) {
                delay = 2 * delay;
            }
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n", delay, -diff);

   //  delay 代表  当前帧实际，实际应该显示多长时间。
    return delay;
}

static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            return vp->duration;
        else
            return duration;
    } else {
        return 0.0;
    }
}

/**
 * 更新视频时钟，记录那时候视频流播放到哪里了
 * @param is
 * @param pts
 * @param pos
 * @param serial
 */
static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
    puts("update_video_pts");
    /* update current video pts */
    set_clock(&is->vidclk, pts, serial);
    sync_clock_to_slave(&is->extclk, &is->vidclk);
}


/**
 *
 * *有两个逻辑**:
 * 第一：FrameQueue 队列无数据可读，取上一帧来渲染SDL窗口，通常是因为调整了窗口大小才会执行 video_display() 重新渲染。
 * 第二: FrameQueue 队列有数据可读，就会跑进去 else{...} 的逻辑，peek 一个帧，看看是否可以播放，
 *      如果可以播放，设置 is->force_refresh 为 1，然后再 执行 video_display() 渲染画面。
 *
 * called to display each frame
 * @param opaque           : VideoState
 * @param remaining_time  : 要播放下一帧视频，还需要等待多少秒。或者说要过多久才去检查一下是否可以播放下一帧。
 */
static void video_refresh(void *opaque, double *remaining_time) {
    VideoState *is = opaque;
    printf("video_refresh(): By :%s\n", SDL_GetThreadName(is->read_tid));
    double time;

    Frame *sp, *sp2;

    // 外部时钟同步
    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
    {
        check_external_clock_speed(is);
    }

    // 音频波形显示
    if (!display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
        time = av_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + rdftspeed < time) {
            video_display(is);
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + rdftspeed - time);
    }

    // 播放视频画面
    if (is->video_st) {
        // 重试啊......
        retry:
        // 帧队列是否为空
        if (frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
            // 队列中没有图像可显示
        } else { // 这个else ，会将 force_refresh= 1;
            double last_duration, duration, delay;
            Frame  *vp, *lastvp;

            /* dequeue the picture */
            // 从队列取出上一个Frame
            lastvp = frame_queue_peek_last(&is->pictq);
            // 读取待显示帧
            vp     = frame_queue_peek(&is->pictq);

            // 当出现了序列号不一致，就不断地 retry，这样做把 无效的视频帧，统统丢掉；
            if (vp->serial != is->videoq.serial) {
                // 如果不是最新的播放序列，则将其出队列，以尽快读取最新序列的帧
                frame_queue_next(&is->pictq);
                goto retry;
            }

            if (lastvp->serial != vp->serial)
                // 新的播放序列重置当前时间
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (is->paused) {
                printf("    video_refresh() : 视频暂停is->paused");
                goto display;
            }


            /* compute nominal last_duration */
            //last_duration 计算上一帧应显示的时长
            last_duration = vp_duration(is, lastvp, vp);
            // 经过compute_target_delay方法，计算出待显示帧vp需要等待的时间
            // case 1 : 如果以video同步，则delay直接等于last_duration。
            // case 2 : 如果以audio或外部时钟同步，则需要比对 主时钟调整待显示帧vp要等待的时间。
            delay         = compute_target_delay(last_duration, is);

            time                = av_gettime_relative() / 1000000.0;
            // is->frame_timer 实际上就是上一帧last vp的播放时间,(vp 是 Frame)
            // is->frame_timer + delay 是待显示帧vp该播放的时间
            if (time < (is->frame_timer + delay)) { //判断是否继续显示上一帧
                // 当前系统时刻还未到达上一帧的结束时刻，那么还应该继续显示上一帧。
                // 计算出最小等待时间
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                goto display;
            }
            // 走到这一步，说明已经到了或过了该显示的时间，待显示帧vp的状态变更为当前要显示的帧
            is->frame_timer += delay;// 更新当前帧播放的时间
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;// 如果和系统时间差距太大，就纠正为系统时间 todo 这TM 怎么纠正？

            SDL_LockMutex(is->pictq.mutex);
            if (!isnan(vp->pts)) {
                // 更新video时钟
                update_video_pts(is, vp->pts, vp->pos, vp->serial);
            }

            SDL_UnlockMutex(is->pictq.mutex);
            // 丢帧逻辑
            if (frame_queue_nb_remaining(&is->pictq) > 1) {// 有nextvp才会检测是否该丢帧
                Frame *nextvp = frame_queue_peek_next(&is->pictq);
                duration = vp_duration(is, vp, nextvp);
                if (!is->step    // 非逐帧模式才检测是否需要丢帧 is->step==1 为逐帧播放
                    && (framedrop > 0 // cpu解帧过慢
                        || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) // 非视频同步方式
                    && time > is->frame_timer + duration // 确实落后了一帧数据
                        ) {

                    // todo __FUNCTION__ 和 __LINE__ 的用法，我忘记了。。。。__LINE__ 是代码行数
                    printf("%s(%d) diff:%lfs, drop frame\n", __FUNCTION__, __LINE__,(is->frame_timer + duration) - time);

                    is->frame_drops_late++;// 统计丢帧情况
                    puts("  video_refresh() : 调用 frame_queue_next() 处理丢帧");
                    frame_queue_next(&is->pictq); // 这里实现真正的丢帧
                    //(这里不能直接while丢帧，因为很可能audio clock重新对时了，这样delay值需要重新计算)
                    goto retry;//回到函数开始位置，继续重试
                }
            }

            if (is->subtitle_st) {
                while (frame_queue_nb_remaining(&is->subpq) > 0) {
                    sp = frame_queue_peek(&is->subpq);

                    if (frame_queue_nb_remaining(&is->subpq) > 1)
                        sp2 = frame_queue_peek_next(&is->subpq);
                    else
                        sp2 = NULL;

                    if (sp->serial != is->subtitleq.serial
                        || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                        || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000)))) {
                        if (sp->uploaded) {
                            int i;
                            for (i = 0; i < sp->sub.num_rects; i++) {
                                AVSubtitleRect *sub_rect = sp->sub.rects[i];
                                uint8_t        *pixels;
                                int            pitch, j;

                                if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *) sub_rect, (void **) &pixels,
                                                     &pitch)) {

                                    for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                        memset(pixels, 0, sub_rect->w << 2);
                                    SDL_UnlockTexture(is->sub_texture);
                                }
                            }
                        }
                        frame_queue_next(&is->subpq);
                    } else {
                        break;
                    }
                }
            }

            //  当前vp帧出队列
            frame_queue_next(&is->pictq);
            /* 说明需要刷新视频帧 */
            is->force_refresh = 1;

            if (is->step && !is->paused) {
                stream_toggle_pause(is);
            }

        }
        // 展示啊......
        display:
        /* display picture */
        if (!display_disable && is->force_refresh
        && is->show_mode == SHOW_MODE_VIDEO
        && is->pictq.rindex_shown // 为了防止 FrameQueue 一帧数据都没有，就调了 video_display()。
                ) {
            // 负责把 视频帧 AVFrame 的数据渲染到 SDL_Texture（纹理）上面。
            video_display(is);// 重点是显示
        }

    }

    // force_refresh = 0 : 需要刷新画面
    is->force_refresh = 0;
    // 打印音视频的同步信息到控制台上。（
    if (show_status) {
        AVBPrint       buf;
        static int64_t last_time;
        int64_t        cur_time;
        int            aqsize, vqsize, sqsize;
        double         av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize      = 0;
            vqsize      = 0;
            sqsize      = 0;
            if (is->audio_st)
                aqsize  = is->audioq.size;
            if (is->video_st)
                vqsize  = is->videoq.size;
            if (is->subtitle_st)
                sqsize  = is->subtitleq.size;
            av_diff     = 0;
            if (is->audio_st && is->video_st)
                av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
            else if (is->video_st)
                av_diff = get_master_clock(is) - get_clock(&is->vidclk);
            else if (is->audio_st)
                av_diff = get_master_clock(is) - get_clock(&is->audclk);

            av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
            av_bprintf(&buf,
                       "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                       get_master_clock(is),
                       (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
                       av_diff,
                       is->frame_drops_early + is->frame_drops_late,
                       aqsize / 1024,
                       vqsize / 1024,
                       sqsize,
                       is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
                       is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);

            if (show_status == 1 && AV_LOG_INFO > av_log_get_level())
                fprintf(stderr, "%s\n", buf.str);
            else
                av_log(NULL, AV_LOG_INFO, "%s\n", buf.str);

            fflush(stderr);
            av_bprint_finalize(&buf, NULL);

            last_time = cur_time;
        }
    }
}


/**
 * 把经过 滤镜处理的 AVFrame 放进 FrameQueue,
 * 链路如下：
 * AVFrame-->Frame-->frame_queue_peek_writable()--->frame_queue_push() ---->
 * @param is
 * @param src_frame
 * @param pts
 * @param duration
 * @param pos
 * @param serial
 * @return
 */
static int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial) {
    puts("queue_picture(): ");
    Frame *vp;

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif

    // 检测队列可写，获取可写Frame指针
    puts("  queue_picture() # 从 VideoState 获取 FrameQueue .");
    if (!(vp = frame_queue_peek_writable(&is->pictq))) {
        perror("队列已满.");
        return -1;
    }


    // 开始对可写的Frame赋值
    // NOTE: 采样比
    vp->sar      = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width  = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts      = pts;
    vp->duration = duration;
    vp->pos      = pos;
    vp->serial   = serial;

    // 设置窗口大小
    set_default_window_size(vp->width, vp->height, vp->sar);


    // 将src_frame中所有数据拷贝到vp->frame
    av_frame_move_ref(vp->frame, src_frame);
    printf("    queue_picture() : current frame pts is : %f s.\n",vp->frame->pts* av_q2d(is->ic->streams[is->video_stream]->time_base));
    //更新写索引位置
    frame_queue_push(&is->pictq);
    return 0;
}

/**
 * 解码AVPacket ，获取链如下：
 *   VideoState--->PacketQueue--->MyAVPacketList--->AVPacket
 * @param is  VideoState
 * @param frame  在video_thread() 函数里 malloc 的AVFrame；
 * @return  解码后的 AVFrame
 */
static int get_video_frame(VideoState *is, AVFrame *frame) {
    puts("get_video_frame()");
    int got_picture;

    if ((got_picture = decoder_decode_frame(&is->viddec, frame, NULL)) < 0) {
        return -1;
    }


    if (got_picture) {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE) {
            dpts = av_q2d(is->video_st->time_base) * frame->pts;
        }


        // sar
        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

        if (framedrop > 0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock(is);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    is->frame_drops_early++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

#if CONFIG_AVFILTER

static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx) {
    int           ret, i;
    int           nb_filters = graph->nb_filters;
    AVFilterInOut *outputs   = NULL, *inputs = NULL;

    if (filtergraph) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name       = av_strdup("out");
        inputs->filter_ctx = sink_ctx;
        inputs->pad_idx    = 0;
        inputs->next       = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
    fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

static int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame) {
    puts("configure_video_filters()");
    enum AVPixelFormat pix_fmts[FF_ARRAY_ELEMS(sdl_texture_format_map)];
    char               sws_flags_str[512] = "";
    char               buffersrc_args[256];
    int               ret;
    AVFilterContext   *filt_src   = NULL, *filt_out = NULL, *last_filter = NULL;
    AVCodecParameters *codecpar   = is->video_st->codecpar;
    AVRational        fr          = av_guess_frame_rate(is->ic, is->video_st, NULL);
    AVDictionaryEntry *e          = NULL;
    int               nb_pix_fmts = 0;
    int               i, j;

    // ffmpeg 像素格式到 SDL 像素格式的映射过程，
    for (i = 0; i < renderer_info.num_texture_formats; i++) {
        for (j = 0; j < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; j++) {
            if (renderer_info.texture_formats[i] == sdl_texture_format_map[j].texture_fmt) {
                pix_fmts[nb_pix_fmts++] = sdl_texture_format_map[j].format;
                break;
            }
        }
    }
    pix_fmts[nb_pix_fmts] = AV_PIX_FMT_NONE;

    while ((e = av_dict_get(sws_dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str) - 1] = '\0';

    graph->scale_sws_opts = av_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             is->video_st->time_base.num, is->video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1));
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;

    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    last_filter = filt_out;

/* Note: this macro adds a filter before the lastly added filter, so the
 * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                          \
    AVFilterContext *filt_ctx;                                               \
                                                                             \
    ret = avfilter_graph_create_filter(&filt_ctx,                            \
                                       avfilter_get_by_name(name),           \
                                       "ffplay_" name, arg, NULL, graph);    \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    last_filter = filt_ctx;                                                  \
} while (0)

    if (autorotate) {
        double theta = get_rotation(is->video_st);

        if (fabs(theta - 90) < 1.0) {
            INSERT_FILT("transpose", "clock");
        } else if (fabs(theta - 180) < 1.0) {
            INSERT_FILT("hflip", NULL);
            INSERT_FILT("vflip", NULL);
        } else if (fabs(theta - 270) < 1.0) {
            INSERT_FILT("transpose", "cclock");
        } else if (fabs(theta) > 1.0) {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
    }

    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;

    is->in_video_filter  = filt_src;
    is->out_video_filter = filt_out;

    fail:
    return ret;
}

/**
 * 创建音频滤镜函数
 * @param is
 * @param afilters   :是滤镜字符串 ， 如："atempo=2.0"
 * @param force_output_format
 * @return
 */
static int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format) {
    puts("configure_audio_filters() : 创建音频滤镜函数.");
    static const enum AVSampleFormat sample_fmts[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE};

    // 定义了 一些只有 2 个元素的数组，这其实是 ffmpeg 项目传递参数的方式，
    // 传递一个数组进去函数，主要有两种方式。
    //  1:  传递数组的大小。就是有多少个元素。
    //  2:  传递数组的结尾，只要读到结尾元素 (-1)，就算结束了。
    int     sample_rates[2]    = {0, -1}; // ffmpeg 大部分函数采用的是第二种方式。
    int64_t channel_layouts[2] = {0, -1}; // ffmpeg 大部分函数采用的是第二种方式。
    int     channels[2]        = {0, -1}; // ffmpeg 大部分函数采用的是第二种方式。

    AVFilterContext   *filt_asrc              = NULL, *filt_asink = NULL;
    char              aresample_swr_opts[512] = "";
    AVDictionaryEntry *e                      = NULL;
    char              asrc_args[256];
    int               ret;

    // is->agraph 一开始确实是 NULL，但是 configure_audio_filters() 这个函数可能会调用第二次，
    // 第二次的时候 is->agraph 就不是 NULL了。
    // configure_audio_filters()
    // 第一次调用是在 stream_component_open() 里面
    // 第二次调用是在 audio_thread()
    avfilter_graph_free(&is->agraph);
    if (!(is->agraph       = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);
    // 设置 滤镜使用的线程数量，0 为自动选择线程数量
    is->agraph->nb_threads = filter_nbthreads;

    while ((e = av_dict_get(swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts) - 1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                   is->audio_filter_src.freq, av_get_sample_fmt_name(is->audio_filter_src.fmt),
                   is->audio_filter_src.channels,
                   1, is->audio_filter_src.freq);
    if (is->audio_filter_src.channel_layout)
        snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
                 ":channel_layout=0x%"PRIx64, is->audio_filter_src.channel_layout);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0)
        goto end;


    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts, AV_SAMPLE_FMT_NONE,
                                   AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format) {
        channel_layouts[0] = is->audio_tgt.channel_layout;
        channels[0]        = is->audio_tgt.channel_layout ? -1 : is->audio_tgt.channels;
        sample_rates[0]    = is->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_counts", channels, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates", sample_rates, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }


    if ((ret = configure_filtergraph(is->agraph, afilters, filt_asrc, filt_asink)) < 0)
        goto end;

    is->in_audio_filter  = filt_asrc;
    is->out_audio_filter = filt_asink;

    end:
    if (ret < 0)
        avfilter_graph_free(&is->agraph);
    return ret;
}

#endif  /* CONFIG_AVFILTER */

/**
 *  从 PacketQueue audioq 队列拿 AVPacket，然后丢给解码器解码，
 *  解码出来 AVFrame 之后，再把 AVFrame 丢到 FrameQueue 队列。
 * @param arg
 * @return
 */
static int audio_thread(void *arg) {
    puts("audio_thread : 从 PacketQueue audioq 队列拿 AVPacket，然后丢给解码器解码，解码出来 AVFrame 之后，再把 AVFrame 丢到 FrameQueue 队列。");
    VideoState *is    = arg;
    AVFrame    *frame = av_frame_alloc();
    Frame      *af;
#if CONFIG_AVFILTER
    int     last_serial = -1;
    int64_t dec_channel_layout;
    int     reconfigure;
#endif

    int        got_frame = 0;
    AVRational tb;
    int        ret       = 0;


    if (!frame){
        return AVERROR(ENOMEM);
    }


    // 循环，从videState(VideoState--> Decoder --->AVPacket) 中拿到 AVPacket ,
    // 然后解码AVPacket 拿到 音频的 AVFrame；
    do {
        if ((got_frame = decoder_decode_frame(&is->auddec, frame, NULL)) < 0) {
            goto the_end;
        }

        if (got_frame) {
            {
                tb = (AVRational) {1, frame->sample_rate};
            }

#if CONFIG_AVFILTER
            puts("      audio_thread : 处理滤镜");
            dec_channel_layout = get_valid_channel_layout(frame->channel_layout, frame->channels);

            // 检测解码出来的AVFrame 音频格式是否和之前穿件的入口滤镜的音频格式 一致
            reconfigure =
                    cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.channels,
                                   frame->format, frame->channels) ||
                    is->audio_filter_src.channel_layout != dec_channel_layout ||
                    is->audio_filter_src.freq != frame->sample_rate ||
                    is->auddec.pkt_serial != last_serial;

            if (reconfigure) {
                char buf1[1024], buf2[1024];
                av_get_channel_layout_string(buf1, sizeof(buf1), -1, is->audio_filter_src.channel_layout);
                av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);
                av_log(NULL, AV_LOG_DEBUG,
                       "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                       is->audio_filter_src.freq, is->audio_filter_src.channels,
                       av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                       frame->sample_rate, frame->channels, av_get_sample_fmt_name(frame->format), buf2,
                       is->auddec.pkt_serial);
                //----log end.

                is->audio_filter_src.fmt            = frame->format;
                is->audio_filter_src.channels       = frame->channels;
                is->audio_filter_src.channel_layout = dec_channel_layout;
                is->audio_filter_src.freq           = frame->sample_rate;
                last_serial = is->auddec.pkt_serial;

                // 重新配置滤镜
                if ((ret = configure_audio_filters(is, afilters, 1)) < 0)
                {
                    goto the_end;
                }
            }

            if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                goto the_end;

            while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                tb = av_buffersink_get_time_base(is->out_audio_filter);
#endif
                if (!(af = frame_queue_peek_writable(&is->sampq)))
                    goto the_end;

                af->pts      = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                af->pos      = frame->pkt_pos;
                // AVPacket 的需要赋值给 AVFrame
                af->serial   = is->auddec.pkt_serial;
                af->duration = av_q2d((AVRational) {frame->nb_samples, frame->sample_rate});

                // 临时new的 frame ，赋值给af->frame
                av_frame_move_ref(af->frame, frame);
                frame_queue_push(&is->sampq);

#if CONFIG_AVFILTER
                if (is->audioq.serial != is->auddec.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                is->auddec.finished = is->auddec.pkt_serial;
#endif
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

    the_end:
// 处理滤镜
#if CONFIG_AVFILTER
    avfilter_graph_free(&is->agraph);
#endif

    av_frame_free(&frame);
    return ret;
}

/**
 * 开启线程，开始解码相关数据
 * @param d
 * @param fn
 * @param thread_name
 * @param arg
 * @return
 */
static int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void *arg) {
    printf("decoder_start() : \"%s : 开启 SDL 解码线程.\n",thread_name);
    packet_queue_start(d->queue);
    d->decoder_tid = SDL_CreateThread(fn, thread_name, arg);
    if (!d->decoder_tid) {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * 从 PacketQueue videoq 队列拿 AVPacket，
 * 然后丢给解码器解码，解码出来 AVFrame 之后，再把 AVFrame 丢到 FrameQueue 队列。
 *  解码函数是 ： get_video_frame()
 * @param arg  It is  VideoState;
 * @return
 */
static int video_thread(void *arg) {
    puts("video_thread()");

    VideoState *is        = arg;
    AVFrame    *frame     = av_frame_alloc();
    double     pts;
    double     duration;
    int        ret;
    AVRational tb         = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

#if CONFIG_AVFILTER
    AVFilterGraph      *graph           = NULL;
    AVFilterContext    *filt_out        = NULL, *filt_in = NULL;
    int                last_w           = 0;
    int                last_h           = 0;
    enum AVPixelFormat last_format      = -2;
    int                last_serial      = -1;
    int                last_vfilter_idx = 0;
#endif

    if (!frame){
        return AVERROR(ENOMEM);
    }

    puts("  video_thread# : 开始循环解码，实际调用 函数 ： get_video_frame()");
    for (;;) {
        ret = get_video_frame(is, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;

#if CONFIG_AVFILTER
        if (last_w != frame->width
            || last_h != frame->height
            || last_format != frame->format
            || last_serial != is->viddec.pkt_serial
            || last_vfilter_idx != is->vfilter_idx) {
            av_log(NULL, AV_LOG_DEBUG,
                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                   last_w, last_h,
                   (const char *) av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                   frame->width, frame->height,
                   (const char *) av_x_if_null(av_get_pix_fmt_name(frame->format), "none"), is->viddec.pkt_serial);
            avfilter_graph_free(&graph);
            graph = avfilter_graph_alloc();
            if (!graph) {
                ret = AVERROR(ENOMEM);
                goto the_end;
            }
            graph->nb_threads = filter_nbthreads;
            if ((ret = configure_video_filters(graph, is, vfilters_list ? vfilters_list[is->vfilter_idx] : NULL,
                                               frame)) < 0) {
                SDL_Event event;
                event.type       = FF_QUIT_EVENT;
                event.user.data1 = is;
                SDL_PushEvent(&event);
                goto the_end;
            }
            filt_in          = is->in_video_filter;
            filt_out         = is->out_video_filter;
            last_w           = frame->width;
            last_h           = frame->height;
            last_format      = frame->format;
            last_serial      = is->viddec.pkt_serial;
            last_vfilter_idx = is->vfilter_idx;
            frame_rate       = av_buffersink_get_frame_rate(filt_out);
        }


        puts("  video_thread# : 将avframe 交给滤镜)");
        ret = av_buffersrc_add_frame(filt_in, frame);
        if (ret < 0)
            goto the_end;

        while (ret >= 0) {
            is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

            puts("      video_thread# : 从buffersink 中 取出滤镜 处理好的avframe)");
            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
            if (ret < 0) {
                if (ret == AVERROR_EOF)
                    is->viddec.finished = is->viddec.pkt_serial;
                ret = 0;
                break;
            }

            is->frame_last_filter_delay     = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;

            if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                is->frame_last_filter_delay = 0;
            tb = av_buffersink_get_time_base(filt_out);
#endif
            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational) {frame_rate.den, frame_rate.num}) : 0);
            pts      = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            ret      = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
            av_frame_unref(frame);
#if CONFIG_AVFILTER
            if (is->videoq.serial != is->viddec.pkt_serial)
                break;
        }
#endif

        if (ret < 0)
            goto the_end;
    }
    the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&graph);
#endif
    av_frame_free(&frame);
    return 0;
}

static int subtitle_thread(void *arg) {
    puts("subtitle_thread() : 字幕线程");
    VideoState *is = arg;
    Frame      *sp;
    int        got_subtitle;
    double     pts;

    for (;;) {
        if (!(sp = frame_queue_peek_writable(&is->subpq)))
            return 0;

        if ((got_subtitle = decoder_decode_frame(&is->subdec, NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double) AV_TIME_BASE;
            sp->pts      = pts;
            sp->serial   = is->subdec.pkt_serial;
            sp->width    = is->subdec.avctx->width;
            sp->height   = is->subdec.avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            frame_queue_push(&is->subpq);
        } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size) {
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len                        = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len                    = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/**
  * 用视频时钟为主时钟进行音视频同步，当音视频不同步的时候，就需要减少或增加音频帧的样本数量，
 * 让音频流能拉长或者缩短，达到音频流能追赶视频流 或者减速慢下来等待视频流追上来 的效果
 * return the wanted number of samples to get better sync if sync_type is video
 * or external master clock
 * @param is
 * @param nb_samples  当前，音频帧，样本采样数
 * @return
 */
static int synchronize_audio(VideoState *is, int nb_samples) {
    puts("synchronize_audio()");
    int wanted_nb_samples = nb_samples;
    printf("    synchronize_audio() # wanted_nb_samples : %d\n",wanted_nb_samples);

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        puts("  synchronize_audio() : 不是按照音频同步时钟.");

        double diff, avg_diff;
        int    min_nb_samples, max_nb_samples;

        // diff 只要小于 0 了，就代表视频比音频播放慢了
        diff = get_clock(&is->audclk) - get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int) (diff * is->audio_src.freq);
                    min_nb_samples    = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples    = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                // log it
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                       diff, avg_diff, wanted_nb_samples - nb_samples,
                       is->audio_clock, is->audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, soreset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;

            perror("A-V 差异太大了，没法同步.");
        }
    }

    return wanted_nb_samples;
}

/**
 * 从 FrameQueue 读取 AVFrame,
 *然后 把 is->audio_buf 指针指向 AVFrame 的 data ，如果经过重采样， is->audio_buf 指针会指向重采样后的数据，也就是 is->audio_buf1。
 *
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
static int audio_decode_frame(VideoState *is) {
    puts("audio_decode_frame()");
    int              data_size, resampled_data_size;
    int64_t          dec_channel_layout;
    av_unused double audio_clock0;
    // 期望的：每一秒中，采样的音频数量
    int              wanted_nb_samples;
    Frame            *af;

    if (is->paused){
        return -1;
    }

    // 丢弃音频帧
    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&is->sampq) == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
             // 获取FrameQueue 队列中的Frame
        if (!(af = frame_queue_peek_readable(&is->sampq)))
        {
            return -1;
        }
        printf("    audio_decode_frame() : 丢弃音频帧\n");
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);


    printf("    audio_decode_frame() ------------------------------------------------------------------------------------------------------|\n");
    printf("    audio_decode_frame()  声道数#channels : %d ,每秒的采样次数#nb_samples : %d , format : %d\n",
           af->frame->channels,
           af->frame->nb_samples,
           af->frame->format);
    printf("    audio_decode_frame() ------------------------------------------------------------------------------------------------------|\n");

    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                           af->frame->nb_samples,
                                           af->frame->format, 1);

    printf("    audio_decode_frame() : sample      : %d.\n", av_get_bytes_per_sample(af->frame->format));
    printf("    audio_decode_frame() : format_name : %s.\n", av_get_sample_fmt_name(af->frame->format));
    printf("    audio_decode_frame() : data_size   : %d.\n", data_size);

    dec_channel_layout =
            (af->frame->channel_layout &&
             af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
            af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);

    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);
    printf("    audio_decode_frame() : wanted_nb_samples   : %d.\n", wanted_nb_samples);

    if (af->frame->format != is->audio_src.fmt ||
        dec_channel_layout != is->audio_src.channel_layout ||
        af->frame->sample_rate != is->audio_src.freq ||
        (wanted_nb_samples != af->frame->nb_samples // 用视频时钟为主时钟进行音视频同步，当音视频不同步的时候，就需要减少或增加音频帧的样本数量，让音频流能拉长或者缩短，达到音频流能追赶视频流 或者减速慢下来等待视频流追上来 的效果
        && !is->swr_ctx)
        ) {
        printf("    audio_decode_frame() : af->frame->nb_samples   :  %d.\n", af->frame->nb_samples);
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(NULL,
                                         is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
                                         dec_channel_layout, af->frame->format, af->frame->sample_rate,
                                         0, NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                   af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->channels,
                   is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            return -1;
        }
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = af->frame->channels;
        is->audio_src.freq           = af->frame->sample_rate;
        is->audio_src.fmt            = af->frame->format;
    }

    if (is->swr_ctx) {
        const uint8_t **in      = (const uint8_t **) af->frame->extended_data;
        uint8_t       **out     = &is->audio_buf1;
        int           out_count = (int64_t) wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        int           out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt,
                                                             0);
        int           len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq /
                                                  af->frame->sample_rate,
                                     wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    } else {
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock    = NAN;
    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

/**
 * prepare a new audio buffer
 *
 * @param opaque
 * @param stream 这个指针是 SDL 内部音频数据内存的指针，只要把数据拷贝到这个指针的地址，就能播放声音了。
 * @param len  此次回调需要写多少字节的数据进去 stream 指针。
 */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len) {
    puts("sdl_audio_callback()");

    VideoState *is = opaque;
    int        audio_size, len1;
    printf("  sdl_audio_callback : len : %d\n",len);
    printf("  sdl_audio_callback : is->audio_buf_size : %d\n",is->audio_buf_size);

    audio_callback_time      = av_gettime_relative();

    // copy 音频数据
    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {

            // 从 FrameQueue 读取 AVFrame
            audio_size          = audio_decode_frame(is);
            if (audio_size < 0) {
                /* if error, just output silence */
                is->audio_buf      = NULL;
                is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
            } else {
                if (is->show_mode != SHOW_MODE_VIDEO)
                    update_sample_display(is, (int16_t *) is->audio_buf, audio_size);
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }

        len1     = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
        {
            len1 = len;
        }

        // 复制音频数据到 SDL的内存
        if (!is->muted && is->audio_buf
        &&is->audio_volume == SDL_MIX_MAXVOLUME  // 以最大音量 copy
        ) {
            int temp_index = is->audio_buf_index;
            memcpy(stream, (uint8_t *) is->audio_buf + temp_index, len1);
        } else {
            memset(stream, 0, len1);
            if (!is->muted && is->audio_buf){
                // 以调整后的音量copy
                // SDL_MixAudioFormat() 函数有调整音量的功能。
                SDL_MixAudioFormat(stream, (uint8_t *) is->audio_buf + is->audio_buf_index, AUDIO_S16SYS, len1,is->audio_volume);
            }
        }
        len -= len1;
        stream += len1;
        // 更新读取位置
        is->audio_buf_index += len1;
    }

    //-------- 设置音频时钟--------
    // 代表当前缓存里还剩多少数据没有拷贝给 SDL
    //  当sdl 内部还剩下 audio_hw_buf_size 字节的时候，就会用回调，来取len 字节，
    // 同时，我们的audio_buf 缓存还剩下audio_write_buf_size 字节，总共有三块内存等待播放，而这三块内存播放完以后的pts 就是 is->audio_clock;
    // 如::  =======audio_hw_buf_size====len=====audio_write_buf_size===，这三块内存
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(is->audio_clock)) {
        // 更新音频时钟
        // is->audio_clock 记录的是播放完那个 AVFrame 之后的 pts，但是此时此刻 只是把 这个 AVFrame 的内存数据拷贝给了 SDL，SDL 还没开始播放呢？
        set_clock_at(&is->audclk, is->audio_clock - (double) (2 * is->audio_hw_buf_size + is->audio_write_buf_size) /
                                                    is->audio_tgt.bytes_per_sec, is->audio_clock_serial,
                     audio_callback_time / 1000000.0);
        sync_clock_to_slave(&is->extclk, &is->audclk);
    }
}

/**
 * 调 SDL_OpenAudioDevice() 打开音频设备
 * NOTE: fplay 有两个处理音频的地方，一个是 滤镜（is->agraph），一个是重采样（is->swr_ctx）
 * @param opaque  : 传递给 SDL 回调函数的参数
 * @param wanted_channel_layout 希望用 这样的采样率，声道数，声道布局打开音频硬件设备。
 * @param wanted_nb_channels   希望用 这样的采样率，声道数，声道布局打开音频硬件设备。
 * @param wanted_sample_rate   希望用 这样的采样率，声道数，声道布局打开音频硬件设备。
 * @param audio_hw_params       实际打开的音频硬件设备的音频格式信息
 *  hw : 是 Hardware 的意思，也就是硬件，不过不是指硬件编解码加速，而是指打开的硬件设备。

 * @return
 */
static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate,
                      struct AudioParams *audio_hw_params) {
    puts("    audio_open() : 调 SDL_OpenAudioDevice() 打开音频设备");
    SDL_AudioSpec wanted_spec, spec;
    const char    *env;
    // next_nb_channels[]，这 其实是一个map表，声道切换映射表。举个例子，
    // 如果音响设备不支持 7 声道的数据播放，肯定不能直接报错，
    // 还要尝试一下其他声道能不能成功打开设备吧。
    // 这个其他声道就是 next_nb_channels[]。
    // next_nb_channels[7] = 6，从7声道切换到6声道打开音频设备
    // next_nb_channels[6] = 4，从6声道切换到4声道打开音频设备
    // next_nb_channels[5] = 6，从5声道切换到6声道打开音频设备
    // next_nb_channels[4] = 2，从4声道切换到2声道打开音频设备
    // next_nb_channels[3] = 6，从3声道切换到6声道打开音频设备
    // next_nb_channels[2] = 1，从双声道切换到单声道打开音频设备
    // next_nb_channels[1] = 0，单声道都打不开音频设备，无法再切换，需要降低采样率播放。
    // next_nb_channels[0] = 0，0声道都打不开音频设备，无法再切换，需要降低采样率播放。

    static const int next_nb_channels[]   = {0, 0, 1, 6, 2, 6, 4, 6};
    // 当切换所有声道都无法成功打开音频设备，就需要从 next_sample_rates[] 取一个比当前更小的采样率来尝试。
    static const int next_sample_rates[]  = {0, 44100, 48000, 96000, 192000};
    // 取 index=4的采样率
    int              next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;
    printf("       audio_open():next_sample_rate_idx : %d\n", next_sample_rate_idx);

    env                = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        // atoi() : 把参数 str 所指向的字符串转换为一个整数（类型为 int 型
        wanted_nb_channels    = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    //  声道布局 跟 声道数是否一致，可能是担心用户在命令行输入错误的参数。
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq     = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        // 倒序
        next_sample_rate_idx--;
    wanted_spec.format   = AUDIO_S16SYS;
    wanted_spec.silence  = 0;
    // SDL_AUDIO_MAX_CALLBACKS_PER_SEC : 代表SDL 每秒调多少次回调函数
    wanted_spec.samples  = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE,
    // 2 << 位移只是想把 样本数数量 变成 2 的指数。这是 SDL 文档建议的，
                                 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
    while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec,
                                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",wanted_spec.channels, wanted_spec.freq, SDL_GetError());

        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            // next_sample_rate_idx--，表示降低采样率
            wanted_spec.freq     = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }



    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt            = AV_SAMPLE_FMT_S16;
    // 数量的音频样本占多少内存。也就是一秒钟要播放多少内存的音频数据。
    audio_hw_params->freq           = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels       = spec.channels;
    audio_hw_params->frame_size     = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1,
                                                                 audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec  = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq,
                                                                 audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    // 指的是 SDL 内部音频数据缓存的大小，
    // 代表 SDL线程执行 sdl_audio_callback() 的时候，SDL 硬件内部还有多少字节音频的数据没有播放。
    return spec.size;
}


/**
 * stream_component_open() 函数主要作用是打开 音频流或者视频流 对应的解码器，
 * 开启解码线程去解码。
 * open a given stream. Return 0 if OK
 * @param is
 * @param stream_index  是 数据流 的索引值
 * @return
 */
static int stream_component_open(VideoState *is, int stream_index) {
    AVFormatContext   *ic                = is->ic;
    AVCodecContext    *avctx;
    const AVCodec     *codec;
    const char        *forced_codec_name = NULL;
    AVDictionary      *opts              = NULL;
    AVDictionaryEntry *t                 = NULL;

    int     sample_rate, nb_channels;
    int64_t channel_layout;
    int     ret           = 0;
    // 低分辨率
    int     stream_lowres = lowres;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    // 设置容器的time_base
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    // 查询解码器
    codec = avcodec_find_decoder(avctx->codec_id);

    // forced_codec_name : 是指，外部，指定了编解码器；
    // 如: ffplay -c:v openh264 juren.mp4
    // 注意:
    switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO   :
            is->last_audio_stream = stream_index;
            forced_codec_name = audio_codec_name;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            is->last_subtitle_stream = stream_index;
            forced_codec_name = subtitle_codec_name;
            break;
        case AVMEDIA_TYPE_VIDEO   :
            is->last_video_stream = stream_index;
            forced_codec_name = video_codec_name;
            break;

        case AVMEDIA_TYPE_UNKNOWN:
            puts("1");
            break;
        case AVMEDIA_TYPE_DATA:
            puts("6");
            break;
        case AVMEDIA_TYPE_ATTACHMENT:
            puts("2");

            break;
        case AVMEDIA_TYPE_NB:
            puts("3");
            break;
    }
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name)
            av_log(NULL, AV_LOG_WARNING,
                   "No codec could be found with name '%s'\n", forced_codec_name);
        else
            av_log(NULL, AV_LOG_WARNING,
                   "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
               codec->max_lowres);
        stream_lowres = codec->max_lowres;
    }
    avctx->lowres = stream_lowres;

    if (fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    // 这个函数实际上就是把命令行参数的相关参数提取出来。

    opts = filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t   = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    is->eof                            = 0;
    // 把流属性设置为不丢弃
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;

    // 对 音频，视频，字幕做了区别处理
    switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER // 判断是否启用 滤镜模块?
        {
            // NOTE: fplay 有两个处理音频的地方，一个是 滤镜（is->agraph），一个是重采样（is->swr_ctx）
            AVFilterContext *sink;

            is->audio_filter_src.freq           = avctx->sample_rate;
            is->audio_filter_src.channels       = avctx->channels;
            is->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
            is->audio_filter_src.fmt            = avctx->sample_fmt;

            // 搞好了 is->in_audio_filter 跟 is->out_audio_filter 两个滤镜
            // 然后播放的时候，需要从 out_audio_filter 读取 AVFrame。
            if ((ret = configure_audio_filters(is, afilters, 0)) < 0)
                goto fail;
            sink     = is->out_audio_filter;

            // 调用实际上就是从 buffsink 出口滤镜里面获取到最后的音频信息。
            // 要不然，参数都是 sink ？
            sample_rate    = av_buffersink_get_sample_rate(sink);
            nb_channels    = av_buffersink_get_channels(sink);
            channel_layout = av_buffersink_get_channel_layout(sink);
        }
#else
            sample_rate    = avctx->sample_rate;
            nb_channels    = avctx->channels;
            channel_layout = avctx->channel_layout;
#endif

            /* prepare audio output */
            // 调 SDL_OpenAudioDevice() 打开音频设备
            if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
                goto fail;
            is->audio_hw_buf_size = ret;
            // is->audio_src: 存储的其实是 buffersink 出口滤镜的音频格式，
            // 但是因为出口滤镜的音频格式可能跟 is->audio_tgt 本身是一样的，
            // 所以就这样写了：is->audio_src         = is->audio_tgt;
            // 采样率等信息，就放在 is->audio_tgt 变量返回。
            is->audio_src         = is->audio_tgt;// AudioParams
            is->audio_buf_size    = 0;
            is->audio_buf_index   = 0;

            /* init averaging filter */
            is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
            is->audio_diff_avg_count = 0;
            /* since we do not have a precise anough audio FIFO fullness,
               we correct audio sync only if larger than this threshold */
            is->audio_diff_threshold = (double) (is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

            is->audio_stream = stream_index;
            is->audio_st     = ic->streams[stream_index];
            stpcpy((char *) &is->auddec.decode_name, "音频解码器");
            if ((ret = decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread)) < 0)
                goto fail;
            if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) &&
                !is->ic->iformat->read_seek) {
                is->auddec.start_pts    = is->audio_st->start_time;
                is->auddec.start_pts_tb = is->audio_st->time_base;
            }
            if ((ret = decoder_start(&is->auddec, audio_thread, "audio_decoder_thread", is)) < 0)
                goto out;
            // 启动音频设备，pause_on : 0;
            SDL_PauseAudioDevice(audio_dev, 0);
            break;
        case AVMEDIA_TYPE_VIDEO:
            is->video_stream = stream_index;
            is->video_st     = ic->streams[stream_index];

            stpcpy((char *) &is->viddec.decode_name, "视频解码器");
            if ((ret = decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread)) < 0)
                goto fail;
            if ((ret = decoder_start(&is->viddec, video_thread, "video_decoder_thread", is)) < 0)
                goto out;
            is->queue_attachments_req = 1;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            is->subtitle_stream = stream_index;
            is->subtitle_st     = ic->streams[stream_index];

            stpcpy((char *) &is->subdec.decode_name, "字幕解码器");
            if ((ret = decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread)) < 0)
                goto fail;
            if ((ret = decoder_start(&is->subdec, subtitle_thread, "subtitle_decoder_thread", is)) < 0)
                goto out;
            break;
        default:
            break;
    }
    goto out;

    fail:
    avcodec_free_context(&avctx);
    out:
    av_dict_free(&opts);

    return ret;
}

static int decode_interrupt_cb(void *ctx) {
    VideoState *is = ctx;
    return is->abort_request;
}

/**
 * 判断 AVPacket 是否够用，就是根据 size 来判断
 *  主要就是确认 队列至少有 MIN_FRAMES 个帧，而且所有帧的播放时长加起来大于 1 秒钟。
 * @param st
 * @param stream_id
 * @param queue
 * @return
 */
static int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
    return stream_id < 0 ||  // case 1:  参数ok ？；
           queue->abort_request || // case 2: 退出 ？
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) || // case 3 ： mp3 封面:
           queue->nb_packets > MIN_FRAMES && (!queue->duration ||
                                              // 而且所有帧的播放时长加起来大于 1 秒钟
                                              av_q2d(st->time_base) * queue->duration > 1.0);
}

static int is_realtime(AVFormatContext *s) {
    if (!strcmp(s->iformat->name, "rtp")
        || !strcmp(s->iformat->name, "rtsp")
        || !strcmp(s->iformat->name, "sdp")
            )
        return 1;

    if (s->pb && (!strncmp(s->url, "rtp:", 4)
                  || !strncmp(s->url, "udp:", 4)
    )
            )
        return 1;
    return 0;
}

/* this thread gets the stream from the disk or the network
 *
 *read thread() 线程在以下两种情况会进入休眠 10ms。

第一种情况：PacketQueue 队列满了，无法再塞数据进去。

第二种情况：超过最小缓存size。

如果在 10ms 内，PacketQueue 队列全部被消耗完毕，audio_thread() 或者 video thread() 线程 没有 AVPakcet 能读了，就需要尽快唤醒 read thread() 线程。

还有，如果进行了 seek 操作，也需要快速把 read thread() 线程 从休眠中唤醒。

所以 SDL_cond *continue_read_thread 条件变量，主要用于 read thread 跟 audio_thread ，video_thread 线程进行通信的。
 *
 * */
static int read_thread(void *arg) {
    puts("read thread : 从 网络或者硬盘里面读取 AVPacket，读取到之后放进去 PacketQueue 队列。\n");
    VideoState        *is               = arg;
    AVFormatContext   *ic               = NULL;
    int               err, i, ret;
    int               st_index[AVMEDIA_TYPE_NB];
    AVPacket          *pkt              = NULL;
    int64_t           stream_start_time;
    int               pkt_in_play_range = 0;
    AVDictionaryEntry *t;
    SDL_mutex         *wait_mutex       = SDL_CreateMutex();
    int               scan_all_pmts_set = 0;
    int64_t           pkt_ts;

    if (!wait_mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    memset(st_index, -1, sizeof(st_index));
    is->eof = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate packet.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ic  = avformat_alloc_context();
    if (!ic) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque   = is;
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }

    err = avformat_open_input(&ic, is->filename, is->iformat, &format_opts);
    if (err < 0) {
        print_error(is->filename, err);
        ret = -1;
        goto fail;
    }
    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }
    is->ic = ic;

    if (genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    av_format_inject_global_side_data(ic);

    if (find_stream_info) {
        AVDictionary **opts          = setup_find_stream_info_opts(ic, codec_opts);
        int          orig_nb_streams = ic->nb_streams;

        err = avformat_find_stream_info(ic, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (err < 0) {
            av_log(NULL, AV_LOG_WARNING,
                   "%s: could not find codec parameters\n", is->filename);
            ret = -1;
            goto fail;
        }
    }

    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    if (seek_by_bytes < 0)
        seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    if (!window_title && (t = av_dict_get(ic->metadata, "title", NULL, 0)))
        window_title = av_asprintf("%s - %s", t->value, input_filename);

    /* if seeking requested, we execute it */
    if (start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret       = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                   is->filename, (double) timestamp / AV_TIME_BASE);
        }
    }

    is->realtime = is_realtime(ic);

    if (show_status)
        av_dump_format(ic, 0, is->filename, 0);

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream         *st  = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }

    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (wanted_stream_spec[i] && st_index[i] == -1) {
            av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", wanted_stream_spec[i],
                   av_get_media_type_string(i));
            st_index[i] = INT_MAX;
        }
    }

    if (!video_disable) {
        st_index[AVMEDIA_TYPE_VIDEO] =
                av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                    st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    }

    if (!audio_disable) {
        st_index[AVMEDIA_TYPE_AUDIO] =
                av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                    st_index[AVMEDIA_TYPE_AUDIO],
                                    st_index[AVMEDIA_TYPE_VIDEO],
                                    NULL, 0);

    }

    if (!video_disable && !subtitle_disable) {
        st_index[AVMEDIA_TYPE_SUBTITLE] =
                av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                    st_index[AVMEDIA_TYPE_SUBTITLE],
                                    (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                     st_index[AVMEDIA_TYPE_AUDIO] :
                                     st_index[AVMEDIA_TYPE_VIDEO]),
                                    NULL, 0);

    }


    is->show_mode = show_mode;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream          *st       = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters *codecpar = st->codecpar;
        AVRational        sar       = av_guess_sample_aspect_ratio(ic, st, NULL);
        if (codecpar->width)
            set_default_window_size(codecpar->width, codecpar->height, sar);
    }

    /* open the streams */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        // 打开音频流
        puts("打开音频流");
        stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        // 打开视频流
        puts("打开视频流");
        ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
    }
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        // 打开字幕流
        puts("打开字幕流");
        stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    if (is->video_stream < 0 && is->audio_stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               is->filename);
        ret = -1;
        goto fail;
    }

    if (infinite_buffer < 0 && is->realtime)
        infinite_buffer = 1;

    for (;;) {
        if (is->abort_request)
            break;
        if (is->paused != is->last_paused) {
            is->last_paused = is->paused;
            if (is->paused) {
                // av_read_pause .只对 播放网络流有效；有些流媒体协议支持暂停操作，
                // 暂停了，服务器就不会再往 ffplay 推送数据，如果想重新推数据，需要调用 av_read_play()
                is->read_pause_return = av_read_pause(ic);
            } else {
                // av_read_pause() 和 av_read_play() 是网络流世界的 卧龙与凤雏，必然成对出现；
                av_read_play(ic);
            }
        }
        //  rtsp 和 mmsh 协议
#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (is->paused &&
            (!strcmp(ic->iformat->name, "rtsp") ||
             // strncmp(input_filename, "mmsh:", 5) : 拿input_filename的前5个字符串做比较
             (ic->pb && !strncmp(input_filename, "mmsh:", 5)))) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            SDL_Delay(10);
            continue;
        }
#endif
        if (is->seek_req) {
            int64_t seek_target = is->seek_pos;
            int64_t seek_min    = is->seek_rel > 0 ? seek_target - is->seek_rel + 2 : INT64_MIN;
            int64_t seek_max    = is->seek_rel < 0 ? seek_target - is->seek_rel - 2 : INT64_MAX;
// FIXME the +-2 is due to rounding being not done in the correct direction in generation
//      of the seek_pos/seek_rel variables

            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", is->ic->url);
            } else {
                if (is->audio_stream >= 0)
                    packet_queue_flush(&is->audioq);
                if (is->subtitle_stream >= 0)
                    packet_queue_flush(&is->subtitleq);
                if (is->video_stream >= 0)
                    packet_queue_flush(&is->videoq);
                if (is->seek_flags & AVSEEK_FLAG_BYTE) {
                    set_clock(&is->extclk, NAN, 0);
                } else {
                    set_clock(&is->extclk, seek_target / (double) AV_TIME_BASE, 0);
                }
            }
            is->seek_req              = 0;
            is->queue_attachments_req = 1;
            is->eof                   = 0;
            if (is->paused)
                step_to_next_frame(is);
        }
        if (is->queue_attachments_req) {
            if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                if ((ret = av_packet_ref(pkt, &is->video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&is->videoq, pkt);
                packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
            }
            is->queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (infinite_buffer < 1 &&
            (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE ||
             // 判断 队列缓存中的 AVPacket 是否够用?
             (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
              stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
              stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)
             ))) {


            /* 够用的话，就 wait 10 ms */
            SDL_LockMutex(wait_mutex);
            // 等待10 ms, todo ,,, is->continue_read_thread 是能让   音频流，视频流，字幕流，3个read 线程都 wait 10ms 吗？
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        if (!is->paused &&
            (!is->audio_st ||
             (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
            (!is->video_st ||
             (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0))) {
            if (loop != 1 && (!loop || --loop)) {
                stream_seek(is, start_time != AV_NOPTS_VALUE ? start_time : 0, 0, 0);
            } else if (autoexit) {
                ret = AVERROR_EOF;
                goto fail;
            }
        }
        // 读取 avPacket
        // 当 队列缓存中的 AVPacket 未满的时候，就会直接去读磁盘数据，把 AVPacket 读出来，
        // 但是也不是读出来就会立即丢进去 PacketQueue 队列，而是会判断一下AVPacket 是否在期待的播放时间范围内
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, pkt, is->audio_stream);
                if (is->subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&is->subtitleq, pkt, is->subtitle_stream);
                is->eof = 1;
            }
            if (ic->pb && ic->pb->error) {
                if (autoexit)
                    goto fail;
                else
                    break;
            }
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        } else {
            is->eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts            = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;

        //-------- 研究 rang time 算法-----start
        // us
        int64_t temp_start_time  = (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0);
        // 时间基
        int     temp_time_base   = av_q2d(ic->streams[pkt->stream_index]->time_base);
        // 时间差
        int     time_diff        = (pkt_ts - temp_start_time) * temp_time_base;
        // 是否在 播放范围？
        int     is_in_play_range = time_diff <= ((double) duration / 1000000);
        printf("        is_in_play_range : %d\n", is_in_play_range);
        //-------- 研究 rang time 算法-----end

        // 判断一下AVPacket 是否在期待的播放时间范围内
        pkt_in_play_range = duration == AV_NOPTS_VALUE ||
                            (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) // step 1: 时间差
                            * av_q2d(ic->streams[pkt->stream_index]->time_base) //step 2:  时间差 x time_base= 一个秒值
                            - (double) (start_time != AV_NOPTS_VALUE ? start_time : 0) /
                              1000000 // step 3:  减去 start_time，得到一个差值
                            <= ((double) duration / 1000000);// step 4:  这个差值和 duration 比较大小

        // 下面的代码，主要围绕将 AVPacket 放进队列中
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
            // AVPacket 放进 音频队列
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                   && !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            // AVPacket 放进 视频队列
            packet_queue_put(&is->videoq, pkt);
        } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
            // AVPacket 放进 字幕队列
            packet_queue_put(&is->subtitleq, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

    ret = 0;
    fail:
    if (ic && !is->ic)
        avformat_close_input(&ic);

    av_packet_free(&pkt);
    if (ret != 0) {
        SDL_Event event;

        event.type       = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    SDL_DestroyMutex(wait_mutex);
    return 0;
}

static VideoState *stream_open(const char *filename, AVInputFormat *iformat) {
    puts("------------------------stream_open()------------------------");
    puts("                              | ");
    puts("                              | ");
    puts("                              ↓ ");
    puts("-------------------------av_mallocz()------------------------");
    puts("                              | ");
    puts("                              | ");
    puts("                              ↓ ");
    puts("-------------------------frame_queue_init()------------------");
    puts("                              | ");
    puts("                              | ");
    puts("                              ↓ ");
    puts("-------------------------packet_queue_init()------------------");
    puts("                              | ");
    puts("                              | ");
    puts("                              ↓ ");
    puts("--------------------------init_clock()------------------------");
    puts("                              | ");
    puts("                              | ");
    puts("                              ↓ ");
    puts("-------------------SDL_CreateThread(read_thread)--------------");
    VideoState *is;

    is = av_mallocz(sizeof(VideoState));
    if (!is)
        return NULL;
    is->last_video_stream    = is->video_stream    = -1;
    is->last_audio_stream    = is->audio_stream    = -1;
    is->last_subtitle_stream = is->subtitle_stream = -1;
    is->filename             = av_strdup(filename);
    if (!is->filename)
        goto fail;
    is->iformat = iformat;
    is->ytop    = 0;
    is->xleft   = 0;

    /* start video display */
    if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
        packet_queue_init(&is->subtitleq) < 0)
        goto fail;

    if (!(is->continue_read_thread = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }

    init_clock(&is->vidclk, &is->videoq.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->extclk, &is->extclk.serial);
    is->audio_clock_serial = -1;
    if (startup_volume < 0)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", startup_volume);
    if (startup_volume > 100)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", startup_volume);
    startup_volume = av_clip(startup_volume, 0, 100);
    startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
    is->audio_volume = startup_volume;
    is->muted        = 0;
    is->av_sync_type = av_sync_type;
    is->read_tid     = SDL_CreateThread(read_thread, "read_thread", is);
    if (!is->read_tid) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
        fail:
        stream_close(is);
        return NULL;
    }
    return is;
}

static void stream_cycle_channel(VideoState *is, int codec_type) {
    puts("stream_cycle_channel(): ");
    AVFormatContext *ic        = is->ic;
    int             start_index, stream_index;
    int             old_index;
    AVStream        *st;
    AVProgram       *p         = NULL;
    int             nb_streams = is->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = is->last_video_stream;
        old_index   = is->video_stream;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = is->last_audio_stream;
        old_index   = is->audio_stream;
    } else {
        start_index = is->last_subtitle_stream;
        old_index   = is->subtitle_stream;
    }
    stream_index               = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, is->video_stream);
        if (p) {
            nb_streams       = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index  = -1;
            stream_index     = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams) {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE) {
                stream_index = -1;
                is->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
                case AVMEDIA_TYPE_AUDIO:
                    if (st->codecpar->sample_rate != 0 &&
                        st->codecpar->channels != 0)
                        goto the_end;
                    break;
                case AVMEDIA_TYPE_VIDEO:
                case AVMEDIA_TYPE_SUBTITLE:
                    goto the_end;
                default:
                    break;
            }
        }
    }
    the_end:
    if (p && stream_index != -1)
        stream_index           = p->stream_index[stream_index];
    av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
           av_get_media_type_string(codec_type),
           old_index,
           stream_index);

    stream_component_close(is, old_index);
    stream_component_open(is, stream_index);
}


static void toggle_full_screen(VideoState *is) {
    is_full_screen = !is_full_screen;
    SDL_SetWindowFullscreen(window, is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void toggle_audio_display(VideoState *is) {
    int next = is->show_mode;
    do {
        next = (next + 1) % SHOW_MODE_NB;
    } while (next != is->show_mode &&
             (next == SHOW_MODE_VIDEO && !is->video_st || next != SHOW_MODE_VIDEO && !is->audio_st));
    if (is->show_mode != next) {
        is->force_refresh = 1;
        is->show_mode     = next;
    }
}

/**
 * 如果没有键盘事件发生, refresh_loop_wait_event() 就不会返回，只会不断循环，不断去播放视频流的画面
 * @param is
 * @param event
 */
static void refresh_loop_wait_event(VideoState *is, SDL_Event *event) {
    puts("refresh_loop_wait_event()");
    double remaining_time = 0.0;
    SDL_PumpEvents();
    // 不断地检查是否
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            cursor_hidden = 1;
        }
        if (remaining_time > 0.0){
            av_usleep((int64_t) (remaining_time * 1000000.0));
        }

        remaining_time = REFRESH_RATE;
        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh)){
            // 在video_refresh() 中，remaining_time 有可能被修改；
            video_refresh(is, &remaining_time);
        }

        SDL_PumpEvents();
    }
}

static void seek_chapter(VideoState *is, int incr) {
    int64_t pos = get_master_clock(is) * AV_TIME_BASE;
    int     i;

    if (!is->ic->nb_chapters)
        return;

    /* find the current chapter */
    for (i = 0; i < is->ic->nb_chapters; i++) {
        AVChapter *ch = is->ic->chapters[i];
        if (av_compare_ts(pos, AV_TIME_BASE_Q, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i      = FFMAX(i, 0);
    if (i >= is->ic->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
                                 AV_TIME_BASE_Q), 0, 0);
}

/* handle an event sent by the GUI */
static void event_loop(VideoState *cur_stream) {
    SDL_Event event;
    double    incr, pos, frac;

    for (;;) {
        double x;
        refresh_loop_wait_event(cur_stream, &event);
        switch (event.type) {
            case SDL_KEYDOWN:
                if (exit_on_keydown || event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                    do_exit(cur_stream);
                    break;
                }
                // If we don't yet have a window, skip all key events, because read_thread might still be initializing...
                if (!cur_stream->width)
                    continue;
                switch (event.key.keysym.sym) {
                    case SDLK_f:
                        toggle_full_screen(cur_stream);
                        cur_stream->force_refresh = 1;
                        break;
                    case SDLK_p:
                    case SDLK_SPACE:
                        toggle_pause(cur_stream);
                        break;
                    case SDLK_m:
                        toggle_mute(cur_stream);
                        break;
                    case SDLK_KP_MULTIPLY:
                    case SDLK_0:
                        update_volume(cur_stream, 1, SDL_VOLUME_STEP);
                        break;
                    case SDLK_KP_DIVIDE:
                    case SDLK_9:
                        update_volume(cur_stream, -1, SDL_VOLUME_STEP);
                        break;
                    case SDLK_s: // S: Step to next frame
                        step_to_next_frame(cur_stream);
                        break;
                    case SDLK_a:
                        stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                        break;
                    case SDLK_v:
                        stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                        break;
                    case SDLK_c:
                        stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                        stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                        stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                        break;
                    case SDLK_t:
                        stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                        break;
                    case SDLK_w:
#if CONFIG_AVFILTER
                        if (cur_stream->show_mode == SHOW_MODE_VIDEO && cur_stream->vfilter_idx < nb_vfilters - 1) {
                            // ++cur_stream->vfilter_idx  : 做了递增操作；
                            if (++cur_stream->vfilter_idx >= nb_vfilters){
                                cur_stream->vfilter_idx = 0;
                            }

                        } else {
                            cur_stream->vfilter_idx = 0;
                            toggle_audio_display(cur_stream);
                        }
#else
                        toggle_audio_display(cur_stream);
#endif
                        break;
                    case SDLK_PAGEUP:
                        if (cur_stream->ic->nb_chapters <= 1) {
                            incr = 600.0;
                            goto do_seek;
                        }
                        seek_chapter(cur_stream, 1);
                        break;
                    case SDLK_PAGEDOWN:
                        if (cur_stream->ic->nb_chapters <= 1) {
                            incr = -600.0;
                            goto do_seek;
                        }
                        seek_chapter(cur_stream, -1);
                        break;
                    case SDLK_LEFT:
                        puts("快退 x10");
                        incr = seek_interval ? -seek_interval : -10.0;
                        goto do_seek;
                    case SDLK_RIGHT:
                        puts("快进 x10" );
                        incr = seek_interval ? seek_interval : 10.0;
                        goto do_seek;
                    case SDLK_UP:
                        incr = 60.0;
                        puts("快进 x60" );
                        goto do_seek;
                    case SDLK_DOWN:
                        incr = -60.0;
                        puts("快退 x60");
                    do_seek:
                        if (seek_by_bytes) {
                            pos     = -1;
                            if (pos < 0 && cur_stream->video_stream >= 0)
                                pos = frame_queue_last_pos(&cur_stream->pictq);
                            if (pos < 0 && cur_stream->audio_stream >= 0)
                                pos = frame_queue_last_pos(&cur_stream->sampq);
                            if (pos < 0)
                                pos = avio_tell(cur_stream->ic->pb);
                            if (cur_stream->ic->bit_rate)
                                incr *= cur_stream->ic->bit_rate / 8.0;
                            else
                                incr *= 180000.0;
                            pos += incr;
                            stream_seek(cur_stream, pos, incr, 1);
                        } else {
                            pos     = get_master_clock(cur_stream);
                            if (isnan(pos))
                                pos = (double) cur_stream->seek_pos / AV_TIME_BASE;
                            pos += incr;
                            if (cur_stream->ic->start_time != AV_NOPTS_VALUE &&
                                pos < cur_stream->ic->start_time / (double) AV_TIME_BASE)
                                pos = cur_stream->ic->start_time / (double) AV_TIME_BASE;
                            stream_seek(cur_stream, (int64_t) (pos * AV_TIME_BASE), (int64_t) (incr * AV_TIME_BASE), 0);
                        }
                        break;
                    default:
                        break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (exit_on_mousedown) {
                    do_exit(cur_stream);
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    static int64_t last_mouse_left_click = 0;
                    if (av_gettime_relative() - last_mouse_left_click <= 500000) {
                        toggle_full_screen(cur_stream);
                        cur_stream->force_refresh = 1;
                        last_mouse_left_click = 0;
                    } else {
                        last_mouse_left_click = av_gettime_relative();
                    }
                }
            case SDL_MOUSEMOTION:
                if (cursor_hidden) {
                    SDL_ShowCursor(1);
                    cursor_hidden = 0;
                }
                cursor_last_shown = av_gettime_relative();
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    if (event.button.button != SDL_BUTTON_RIGHT)
                        break;
                    x = event.button.x;
                } else {
                    if (!(event.motion.state & SDL_BUTTON_RMASK))
                        break;
                    x = event.motion.x;
                }
                if (seek_by_bytes || cur_stream->ic->duration <= 0) {
                    uint64_t size = avio_size(cur_stream->ic->pb);
                    stream_seek(cur_stream, size * x / cur_stream->width, 0, 1);
                } else {
                    int64_t ts;
                    int     ns, hh, mm, ss;
                    int     tns, thh, tmm, tss;
                    tns  = cur_stream->ic->duration / 1000000LL;
                    thh  = tns / 3600;
                    tmm  = (tns % 3600) / 60;
                    tss  = (tns % 60);
                    frac = x / cur_stream->width;
                    ns   = frac * tns;
                    hh   = ns / 3600;
                    mm   = (ns % 3600) / 60;
                    ss   = (ns % 60);
                    av_log(NULL, AV_LOG_INFO,
                           "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac * 100,
                           hh, mm, ss, thh, tmm, tss);
                    ts = frac * cur_stream->ic->duration;
                    if (cur_stream->ic->start_time != AV_NOPTS_VALUE)
                        ts += cur_stream->ic->start_time;
                    stream_seek(cur_stream, ts, 0, 0);
                }
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        screen_width              = cur_stream->width  = event.window.data1;
                        screen_height             = cur_stream->height = event.window.data2;
                        if (cur_stream->vis_texture) {
                            SDL_DestroyTexture(cur_stream->vis_texture);
                            cur_stream->vis_texture = NULL;
                        }
                    case SDL_WINDOWEVENT_EXPOSED:
                        cur_stream->force_refresh = 1;
                }
                break;
            case SDL_QUIT:
            case FF_QUIT_EVENT:
                do_exit(cur_stream);
                break;
            default:
                break;
        }
    }
}

static int opt_frame_size(void *optctx, const char *opt, const char *arg) {
    av_log(NULL, AV_LOG_WARNING, "Option -s is deprecated, use -video_size.\n");
    return opt_default(NULL, "video_size", arg);
}

static int opt_width(void *optctx, const char *opt, const char *arg) {
    screen_width = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static int opt_height(void *optctx, const char *opt, const char *arg) {
    screen_height = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static int opt_format(void *optctx, const char *opt, const char *arg) {
    file_iformat = av_find_input_format(arg);
    if (!file_iformat) {
        av_log(NULL, AV_LOG_FATAL, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_frame_pix_fmt(void *optctx, const char *opt, const char *arg) {
    av_log(NULL, AV_LOG_WARNING, "Option -pix_fmt is deprecated, use -pixel_format.\n");
    return opt_default(NULL, "pixel_format", arg);
}

static int opt_sync(void *optctx, const char *opt, const char *arg) {
    if (!strcmp(arg, "audio"))
        av_sync_type = AV_SYNC_AUDIO_MASTER;
    else if (!strcmp(arg, "video"))
        av_sync_type = AV_SYNC_VIDEO_MASTER;
    else if (!strcmp(arg, "ext"))
        av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
    else {
        av_log(NULL, AV_LOG_ERROR, "Unknown value for %s: %s\n", opt, arg);
        exit(1);
    }
    return 0;
}

static int opt_seek(void *optctx, const char *opt, const char *arg) {
    start_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_duration(void *optctx, const char *opt, const char *arg) {
    duration = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_show_mode(void *optctx, const char *opt, const char *arg) {
    show_mode = !strcmp(arg, "video") ? SHOW_MODE_VIDEO :
                !strcmp(arg, "waves") ? SHOW_MODE_WAVES :
                !strcmp(arg, "rdft") ? SHOW_MODE_RDFT :
                parse_number_or_die(opt, arg, OPT_INT, 0, SHOW_MODE_NB - 1);
    return 0;
}

static void opt_input_file(void *optctx, const char *filename) {
    if (input_filename) {
        av_log(NULL, AV_LOG_FATAL,
               "Argument '%s' provided as input filename, but '%s' was already specified.\n",
               filename, input_filename);
        exit(1);
    }
    if (!strcmp(filename, "-"))
        filename   = "pipe:";
    input_filename = filename;
}

static int opt_codec(void *optctx, const char *opt, const char *arg) {
    const char *spec = strchr(opt, ':');
    if (!spec) {
        av_log(NULL, AV_LOG_ERROR,
               "No media specifier was specified in '%s' in option '%s'\n",
               arg, opt);
        return AVERROR(EINVAL);
    }
    spec++;
    switch (spec[0]) {
        case 'a' :
            audio_codec_name = arg;
            break;
        case 's' :
            subtitle_codec_name = arg;
            break;
        case 'v' :
            video_codec_name = arg;
            break;
        default:
            av_log(NULL, AV_LOG_ERROR,
                   "Invalid media specifier '%s' in option '%s'\n", spec, opt);
            return AVERROR(EINVAL);
    }
    return 0;
}

static int dummy;

static const OptionDef options[] = {
        CMDUTILS_COMMON_OPTIONS
        {"x", HAS_ARG, {.func_arg = opt_width}, "force displayed width", "width"},
        {"y", HAS_ARG, {.func_arg = opt_height}, "force displayed height", "height"},
        {"s", HAS_ARG | OPT_VIDEO, {.func_arg = opt_frame_size}, "set frame size (WxH or abbreviation)", "size"},
        {"fs", OPT_BOOL, {&is_full_screen}, "force full screen"},
        {"an", OPT_BOOL, {&audio_disable}, "disable audio"},
        {"vn", OPT_BOOL, {&video_disable}, "disable video"},
        {"sn", OPT_BOOL, {&subtitle_disable}, "disable subtitling"},
        {"ast", OPT_STRING | HAS_ARG | OPT_EXPERT, {&wanted_stream_spec[AVMEDIA_TYPE_AUDIO]},
         "select desired audio stream", "stream_specifier"},
        {"vst", OPT_STRING | HAS_ARG | OPT_EXPERT, {&wanted_stream_spec[AVMEDIA_TYPE_VIDEO]},
         "select desired video stream", "stream_specifier"},
        {"sst", OPT_STRING | HAS_ARG | OPT_EXPERT, {&wanted_stream_spec[AVMEDIA_TYPE_SUBTITLE]},
         "select desired subtitle stream", "stream_specifier"},
        {"ss", HAS_ARG, {.func_arg = opt_seek}, "seek to a given position in seconds", "pos"},
        {"t", HAS_ARG, {.func_arg = opt_duration}, "play  \"duration\" seconds of audio/video", "duration"},
        {"bytes", OPT_INT | HAS_ARG, {&seek_by_bytes}, "seek by bytes 0=off 1=on -1=auto", "val"},
        {"seek_interval", OPT_FLOAT | HAS_ARG, {&seek_interval}, "set seek interval for left/right keys, in seconds",
         "seconds"},
        {"nodisp", OPT_BOOL, {&display_disable}, "disable graphical display"},
        {"noborder", OPT_BOOL, {&borderless}, "borderless window"},
        {"alwaysontop", OPT_BOOL, {&alwaysontop}, "window always on top"},
        {"volume", OPT_INT | HAS_ARG, {&startup_volume}, "set startup volume 0=min 100=max", "volume"},
        {"f", HAS_ARG, {.func_arg = opt_format}, "force format", "fmt"},
        {"pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {.func_arg = opt_frame_pix_fmt}, "set pixel format", "format"},
        {"stats", OPT_BOOL | OPT_EXPERT, {&show_status}, "show status", ""},
        {"fast", OPT_BOOL | OPT_EXPERT, {&fast}, "non spec compliant optimizations", ""},
        {"genpts", OPT_BOOL | OPT_EXPERT, {&genpts}, "generate pts", ""},
        {"drp", OPT_INT | HAS_ARG | OPT_EXPERT, {&decoder_reorder_pts}, "let decoder reorder pts 0=off 1=on -1=auto",
         ""},
        {"lowres", OPT_INT | HAS_ARG | OPT_EXPERT, {&lowres}, "", ""},
        {"sync", HAS_ARG | OPT_EXPERT, {.func_arg = opt_sync}, "set audio-video sync. type (type=audio/video/ext)",
         "type"},
        {"autoexit", OPT_BOOL | OPT_EXPERT, {&autoexit}, "exit at the end", ""},
        {"exitonkeydown", OPT_BOOL | OPT_EXPERT, {&exit_on_keydown}, "exit on key down", ""},
        {"exitonmousedown", OPT_BOOL | OPT_EXPERT, {&exit_on_mousedown}, "exit on mouse down", ""},
        {"loop", OPT_INT | HAS_ARG | OPT_EXPERT, {&loop}, "set number of times the playback shall be looped",
         "loop count"},
        {"framedrop", OPT_BOOL | OPT_EXPERT, {&framedrop}, "drop frames when cpu is too slow", ""},
        {"infbuf", OPT_BOOL | OPT_EXPERT, {&infinite_buffer},
         "don't limit the input buffer size (useful with realtime streams)", ""},
        {"window_title", OPT_STRING | HAS_ARG, {&window_title}, "set window title", "window title"},
        {"left", OPT_INT | HAS_ARG | OPT_EXPERT, {&screen_left}, "set the x position for the left of the window",
         "x pos"},
        {"top", OPT_INT | HAS_ARG | OPT_EXPERT, {&screen_top}, "set the y position for the top of the window", "y pos"},
#if CONFIG_AVFILTER
        {"vf", OPT_EXPERT | HAS_ARG, {.func_arg = opt_add_vfilter}, "set video filters", "filter_graph"},
        {"af", OPT_STRING | HAS_ARG, {&afilters}, "set audio filters", "filter_graph"},
#endif
        {"rdftspeed", OPT_INT | HAS_ARG | OPT_AUDIO | OPT_EXPERT, {&rdftspeed}, "rdft speed", "msecs"},
        {"showmode", HAS_ARG, {.func_arg = opt_show_mode}, "select show mode (0 = video, 1 = waves, 2 = RDFT)", "mode"},
        {"default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {.func_arg = opt_default}, "generic catch all option",
         ""},
        {"i", OPT_BOOL, {&dummy}, "read specified file", "input_file"},
        {"codec", HAS_ARG, {.func_arg = opt_codec}, "force decoder", "decoder_name"},
        {"acodec", HAS_ARG | OPT_STRING | OPT_EXPERT, {&audio_codec_name}, "force audio decoder", "decoder_name"},
        {"scodec", HAS_ARG | OPT_STRING | OPT_EXPERT, {&subtitle_codec_name}, "force subtitle decoder", "decoder_name"},
        {"vcodec", HAS_ARG | OPT_STRING | OPT_EXPERT, {&video_codec_name}, "force video decoder", "decoder_name"},
        {"autorotate", OPT_BOOL, {&autorotate}, "automatically rotate video", ""},
        {"find_stream_info", OPT_BOOL | OPT_INPUT | OPT_EXPERT, {&find_stream_info},
         "read and decode the streams to fill missing information with heuristics"},
        {"filter_threads", HAS_ARG | OPT_INT | OPT_EXPERT, {&filter_nbthreads}, "number of filter threads per graph"},
        {NULL,},
};

static void show_usage(void) {
    av_log(NULL, AV_LOG_INFO, "Simple media player\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [options] input_file\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

void show_help_default(const char *opt, const char *arg) {
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, OPT_EXPERT, 0);
    show_help_options(options, "Advanced options:", OPT_EXPERT, 0, 0);
    printf("\n");
    show_help_children(avcodec_get_class(), AV_OPT_FLAG_DECODING_PARAM);
    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
#if !CONFIG_AVFILTER
    show_help_children(sws_get_class(), AV_OPT_FLAG_ENCODING_PARAM);
#else
    show_help_children(avfilter_get_class(), AV_OPT_FLAG_FILTERING_PARAM);
#endif
    printf("\nWhile playing:\n"
           "q, ESC              quit\n"
           "f                   toggle full screen\n"
           "p, SPC              pause\n"
           "m                   toggle mute\n"
           "9, 0                decrease and increase volume respectively\n"
           "/, *                decrease and increase volume respectively\n"
           "a                   cycle audio channel in the current program\n"
           "v                   cycle video channel\n"
           "t                   cycle subtitle channel in the current program\n"
           "c                   cycle program\n"
           "w                   cycle video filters or show modes\n"
           "s                   activate frame-step mode\n"
           "left/right          seek backward/forward 10 seconds or to custom interval if -seek_interval is set\n"
           "down/up             seek backward/forward 1 minute\n"
           "page down/page up   seek backward/forward 10 minutes\n"
           "right mouse click   seek to percentage in file corresponding to fraction of width\n"
           "left double-click   toggle full screen\n"
    );
}

/* Called from the main */
int main(int argc, char **argv) {
    int        flags;
    VideoState *is;

    init_dynload();

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

    /* register all codecs, demux and protocols */
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();

    init_opts();

    signal(SIGINT, sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

    show_banner(argc, argv, options);

    parse_options(NULL, argc, argv, options, opt_input_file);
    av_log(NULL, AV_LOG_FATAL, "输入的文件路径: %s\n", input_filename);

    if (!input_filename) {
        show_usage();
        av_log(NULL, AV_LOG_FATAL, "An input file must be specified\n");
        av_log(NULL, AV_LOG_FATAL,
               "Use -h to get full help or, even better, run 'man %s'\n", program_name);
        exit(1);
    }

    if (display_disable) {
        video_disable = 1;
    }
    // todo 这里面的 或 运算 该怎么理解?
    flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (audio_disable)
        flags &= ~SDL_INIT_AUDIO;
    else {
        /* Try to work around an occasional ALSA buffer underflow issue when the
         * period size is NPOT due to ALSA resampling by forcing the buffer size. */
        if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
            SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
    }
    if (display_disable)
        flags &= ~SDL_INIT_VIDEO;
    if (SDL_Init(flags)) {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        exit(1);
    }

    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    if (!display_disable) {
        int flags = SDL_WINDOW_HIDDEN;
        if (alwaysontop)
#if SDL_VERSION_ATLEAST(2, 0, 5)
            flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#else
        av_log(NULL, AV_LOG_WARNING, "Your SDL version doesn't support SDL_WINDOW_ALWAYS_ON_TOP. Feature will be inactive.\n");
#endif
        if (borderless)
            flags |= SDL_WINDOW_BORDERLESS;
        else
            flags |= SDL_WINDOW_RESIZABLE;
        window    = SDL_CreateWindow(program_name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, default_width,
                                     default_height, flags);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        if (window) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer) {
                av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n",
                       SDL_GetError());
                renderer = SDL_CreateRenderer(window, -1, 0);
            }
            if (renderer) {
                if (!SDL_GetRendererInfo(renderer, &renderer_info))
                    av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", renderer_info.name);
            }
        }
        if (!window || !renderer || !renderer_info.num_texture_formats) {
            av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
            do_exit(NULL);
        }
    }

    is = stream_open(input_filename, file_iformat);
    if (!is) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        do_exit(NULL);
    }

    event_loop(is);

    /* never returns */

    return 0;
}
