

#include <libavformat/avformat.h>

#include <time.h>

#define  fileName  "/Users/dev/Desktop/mp4/貳佰《玫瑰》1080P.mp4"

int main() {
    fprintf(stdout, "invoke main function\n");

    AVFormatContext *avFormatContext = NULL;
    int type = 1;
    int err;

    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        printf("error code is : %d \n", AVERROR(ENOMEM));
        avformat_close_input(&avFormatContext);
        return 1;
    }
    if (type == 1) {

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
        // close
        avformat_close_input(&avFormatContext);
        return 0;
    }

    if (type == 2) {
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
        for (int i = 0; i < avFormatContext->nb_streams; ++i) {
            int codecType = avFormatContext->streams[i]->codecpar->codec_type;
            printf("stream codec type :%d\n", codecType);
        }
        printf("iformat  name      :%s\n", avFormatContext->iformat->name);
        printf("iformat  long name :%s\n", avFormatContext->iformat->long_name);
        av_dict_free(&format_opts);
        avformat_close_input(&avFormatContext);
    }


    return 0;
}
