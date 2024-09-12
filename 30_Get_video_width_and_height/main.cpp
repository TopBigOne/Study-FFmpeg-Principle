#include <iostream>


extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/eval.h>
#include <libavutil/display.h>
}

//#define  fileName  "/Volumes/Disk2/Downloads/20240910_141706.mp4"
//#define  fileName  "/Users/dev/Desktop/yu_fan/ori_20240910_141706.mp4"
#define  fileName  "/Users/dev/Desktop/yu_fan/flv_格雷西西西_別聽悲傷的歌.flv"


/**
 * 获取视频总时长：s
 * @return
 */
double getVideoDuration() {
    puts(__func__);
    AVFormatContext *formatContext = avformat_alloc_context();
    if (!formatContext) {
        // 处理内存分配错误
        return -1;
    }

    if (avformat_open_input(&formatContext, fileName, nullptr, nullptr) != 0) {
        // 处理打开文件错误
        return -1;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        // 处理查找流信息错误
        return -1;
    }

    int64_t duration = formatContext->duration / AV_TIME_BASE; // 将微秒转换为秒

    avformat_close_input(&formatContext);

    // 输出视频时长
    std::cout << "视频文件总时长为: " << duration << " 秒" << std::endl;
    return duration;

}


double get_rotation(AVStream *st) {
    AVDictionaryEntry *rotate_tag    = av_dict_get(st->metadata, "rotate", NULL, 0);
    uint8_t           *displaymatrix =
                              av_stream_get_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
    double            theta          = 0;

    if (rotate_tag && *rotate_tag->value && strcmp(rotate_tag->value, "0")) {
        char *tail;
        theta     = av_strtod(rotate_tag->value, &tail);
        if (*tail)
            theta = 0;
    }
    if (displaymatrix && !theta)
        theta                        = -av_display_rotation_get(reinterpret_cast<int32_t *>(displaymatrix));

    theta -= 360 * floor(theta / 360 + 0.9 / 360);

    if (fabs(theta - 90 * round(theta / 90)) > 2)
        printf(
                "av_strtod\n Odd rotation angle.\n"
                "If you want to help, upload a sample "
                "of this file to ftp://upload.ffmpeg.org/incoming/ "
                "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

    return theta;
}

int checkVideoInfo() {

    AVFormatContext *avFormatContext = NULL;
    int             type             = 1;
    int             err;

    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        printf("error code is : %d \n", AVERROR(ENOMEM));
        avformat_close_input(&avFormatContext);
        return 1;
    }

    if ((err = avformat_open_input(&avFormatContext, fileName, NULL, NULL)) < 0) {
        fprintf(stderr, "error code is %d", err);
        return 0;
    }
    printf("open success\n");
    printf("file name  :%s\n", avFormatContext->url);
    printf("duration   :%lld\n", avFormatContext->duration);
    printf("nu_streams :%u \n", avFormatContext->nb_streams);

    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        printf("stream codec_type :%d\n", avFormatContext->streams[i]->codecpar->codec_type);
    }
    printf("iformat name      :%s\n", avFormatContext->iformat->name);
    printf("iformat long name :%s\n", avFormatContext->iformat->long_name);



    // 设置探测大小
    AVDictionary *format_opts = NULL;
    av_dict_set(&format_opts, "probesize", "32", 0);
    if ((err = avformat_open_input(&avFormatContext, fileName, NULL, &format_opts)) < 0) {
        fprintf(stderr, "errcode is %d\n", err);
        av_dict_free(&format_opts);
        avformat_close_input(&avFormatContext);
        return 0;
    }
    // 探测文件，get the stream info;
    avformat_find_stream_info(avFormatContext, NULL);
    printf("open success\n");
    printf("file name   :%s\n", avFormatContext->filename);
    printf("duration    :%lld\n", avFormatContext->duration);
    printf("nb_streams  :%d\n", avFormatContext->nb_streams);

    int videoStreamIndex = -1;

    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        int codecType = avFormatContext->streams[i]->codecpar->codec_type;
        printf("stream codec type :%d\n", codecType);

        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;

        }
    }
    printf("iformat  name      :%s\n", avFormatContext->iformat->name);
    printf("iformat  long name :%s\n", avFormatContext->iformat->long_name);


    // 获取视频宽度和高度
    int videoWidth  = avFormatContext->streams[videoStreamIndex]->codecpar->width;
    int videoHeight = avFormatContext->streams[videoStreamIndex]->codecpar->height;
    // 输出视频宽度和高度
    printf("Video Width : %d\n", videoWidth);
    printf("Video Height: %d\n", videoHeight);



    // 获取旋转信息
    double rotation = get_rotation(avFormatContext->streams[videoStreamIndex]);
    printf("Video Rotation : %f degrees\n", rotation);

    AVRational sar       = avFormatContext->streams[videoStreamIndex]->sample_aspect_ratio;
    int        sarWidth  = sar.num;
    int        sarHeight = sar.den;
    printf("Sample Aspect Ratio : %d:%d\n", sarWidth, sarHeight);


    av_dict_free(&format_opts);
    // close
    avformat_close_input(&avFormatContext);

    return 1;

    // /Users/dev/Documents/Android_work/UNI_UBI/Work_1/ijkplayer/ijkmedia/ijkplayer/pipeline
    // /Users/dev/Documents/Android_work/UNI_UBI/Work_1/ijkplayer/ijkmedia/ijkplayer/android
}

int main() {

    checkVideoInfo();
    getVideoDuration();


    return 0;
}
