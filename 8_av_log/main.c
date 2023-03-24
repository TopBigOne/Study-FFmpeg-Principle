#include <stdio.h>

#include <libavformat/avformat.h>

int main() {
    av_log(NULL, AV_LOG_FATAL, "筱雅，你好！\n");
    av_log(NULL, AV_LOG_WARNING, "筱雅，你好！\n");
    av_log(NULL, AV_LOG_ERROR, "筱雅，你好！\n");
    return 0;
}
