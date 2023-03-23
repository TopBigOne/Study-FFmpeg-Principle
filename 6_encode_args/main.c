#include <stdio.h>

#include <libavformat/avformat.h>

#define  VIDEO_PATH "/Users/dev/Desktop/mp4/貳佰《玫瑰》1080P.mp4"

int main() {

    int result = -1;

    AVFormatContext *avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        fprintf(stderr, " avFormatContext in ERROR");
        return 0;
    }

   //  AVDictionary *avDictionary; //todo  这种写法，会报 EXC_BAD_ACCESS (code=1, address=0xb979000104d8eb74)
     AVDictionary *avDictionary = NULL;
    // flags 是编码器的共有属性
    result = av_dict_set(&avDictionary, "flags", "unaligned", AV_DICT_MATCH_CASE);
    if (result < 0) {
        fprintf(stderr, "flags : av_dict_set in ERROR");
        return 0;
    }

    // preset 是libx264编码器的私有属性
    result = av_dict_set(&avDictionary, "preset", "superfast", AV_DICT_MATCH_CASE);
    if (result < 0) {
        fprintf(stderr, "preset : av_dict_set in ERROR");
        return 0;
    }


    result = avformat_open_input(&avFormatContext, VIDEO_PATH, NULL, NULL);
    if (result != 0) {
        fprintf(stderr, " avformat_open_input in ERROR");
        return 0;
    }

    AVCodec *encodeCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (encodeCodec == NULL) {
        fprintf(stderr, " encodeCodec in ERROR");
        return 0;
    }
    AVCodecContext *codecContext = avcodec_alloc_context3(encodeCodec);
    if (codecContext == NULL) {
        fprintf(stderr, " codecContext in ERROR");
        return 0;
    }

    result = avcodec_open2(codecContext, encodeCodec, &avDictionary);
    fprintf(stdout, " avcodec_open2 result : %d\n", result);

    return 0;
}
