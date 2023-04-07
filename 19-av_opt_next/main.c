#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavutil/opt.h>

int main() {
    AVCodec        *avCodec        = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext *avCodecContext = avcodec_alloc_context3(avCodec);
    const AVOption *opt            = NULL;

    // 参数
    printf("打印编码器 共有参数：\n");
    while ((opt = av_opt_next(avCodecContext, opt)) != NULL) {
        printf("common opt name is : %s\n", opt->name);
    }

    printf("-----------------------------------------------------|\n");
    printf("打印编码器 私有参数：\n");
    // 私有参数
    ;
    while ((opt = av_opt_next(avCodecContext->priv_data, opt))) {
        printf("private opt_name is %s\n", opt->name);
        opt = av_opt_next(avCodecContext->priv_data, opt);
    }
    avcodec_free_context(&avCodecContext);


    return 0;
}
