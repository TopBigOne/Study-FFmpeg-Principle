#include <stdio.h>

#define  video_path  "/Users/dev/Desktop/mp4/貳佰《玫瑰》1080P.mp4"

#include <libavformat/avformat.h>

int main() {

    AVFormatContext *avFormatContext = NULL;
    int err;
    avFormatContext = avformat_alloc_context();

    if (!avFormatContext) {
        av_log(NULL, AV_LOG_ERROR, "error code %d\n", AVERROR(ENOMEM));
        avformat_free_context(avFormatContext);
        return 1;
    }
    AVDictionary *format_opsts = NULL;
    AVDictionaryEntry *t;
    av_dict_set(&format_opsts, "formatprobesize", "10485670", AV_DICT_MATCH_CASE);
    av_dict_set(&format_opsts, "export_all", "1", AV_DICT_MATCH_CASE);
    // 故意写错：export_666 是故意写错的，用不上，所以不会被删除；

    av_dict_set(&format_opsts, "export_666", "1", AV_DICT_MATCH_CASE);
    // 获取字典里的每一个属性
    if ((t = av_dict_get(format_opsts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_INFO, "Option key : %s ,value : %s\n", t->key, t->value);
    }
    if ((err = avformat_open_input(&avFormatContext, video_path, NULL, &format_opsts)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "error code is : %d", err);
    } else {
        av_log(NULL, AV_LOG_INFO, "open success.\n");
        av_log(NULL, AV_LOG_INFO, "duration : %lld\n", avFormatContext->duration);
    }
    // 再次获取字典里的第一个属性
    // 为了验证： 已经使用过的属性，会从字典里面删除；
    if ((t = av_dict_get(format_opsts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_INFO, "option key : %s,value : %s\n", t->key, t->value);
    }

    av_dict_free(&format_opsts);


    return 0;
}
