//
// Created by dev on 2023/4/26.
//
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_PATH "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/27_parse_h264_NAL_with_mp4/juren-30s.mp4"
#define H264_FILE_PATH "/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/27_parse_h264_NAL_with_mp4/doc/result_h264.h264"

typedef enum {
    NALU_TYPE_SLICE    = 1,
    NALU_TYPE_DPA      = 2,
    NALU_TYPE_DPB      = 3,
    NALU_TYPE_DPC      = 4,
    NALU_TYPE_IDR      = 5,
    NALU_TYPE_SEI      = 6,
    NALU_TYPE_SPS      = 7,
    NALU_TYPE_PPS      = 8,
    NALU_TYPE_AUD      = 9,
    NALU_TYPE_EOSEQ    = 10,
    NALU_TYPE_EOSTREAM = 11,
    NALU_TYPE_FILL     = 12,
} NaluType;


typedef enum {
    NALU_PRIORITY_DISPOSABLE = 0,
    NALU_PRIRITY_LOW         = 1,
    NALU_PRIORITY_HIGH       = 2,
    NALU_PRIORITY_HIGHEST    = 3
} NaluPriority;

typedef struct {
    int      startcodeprefix_len;
    int      forbidden_bit;
    int      nal_reference_idc;
    int      nal_unit_type;
    unsigned len;
    unsigned max_size;
    char     *buf;
} NALU_t;

//H264码流文件
FILE *h264bitstream         = NULL;
// 用于判断是否是起始码
// 三字节序列 为起始码
int  is_three_byte_sequence = 0;
// 四字节序列 为起始码
int  is_four_byte_sequence  = 0;

/**
 * 判断三字节序列 为起始码
 * @param Buf
 * @return
 */
static int FindStartCode2(unsigned char *Buf) {
    if (Buf[0] != 0 || Buf[1] != 0 || Buf[2] != 1) {
        return 0; //0x000001?
    }
    puts("                                                          FindStartCode2 : 👌🏻牛逼，找到 h264 码流的 start code 0x 001");
    return 1;
}

//0不是起始码;1是起始码
/**
 * 判断四字节序列 为起始码
 * @param Buf
 * @return
 */
static int FindStartCode3(unsigned char *Buf) {
    if (Buf[0] != 0 || Buf[1] != 0 || Buf[2] != 0 || Buf[3] != 1) {
        //0x00000001?
        return 0;
    }
    puts("                                                          FindStartCode3 : 👌牛逼，找到 h264 码流的 start code 0x 0001");
    return 1;
}


/**
 * 获取码流中的NALU数据
 * @param nalu
 * @return   返回-1 : 代表没找到起始码;
 *               0 :代表读文件出错或者calloc出错;
 *               其它返回带起始码的nalu长度，
 *               但是参数nalu->len是记录着不带起始码的长度
 */
int get_annexb_nalu(NALU_t *nalu) {
    //准备环节
    int nalu_index = 0; //NALU的下标
    int StartCodeFound; //用于标记是否找到下一NALU的起始码
    int rewind; //下一起始码的位数,nalu_index+rewind为下一起始码的首字节

    unsigned char *nalu_buffer;// 临时缓存,用于每次存储一个NALU
    if ((nalu_buffer = (unsigned char *) calloc(nalu->max_size, sizeof(char))) == NULL) {
        perror("get_annexb_nalu: Could not allocate nalu_buffer memory\n");
        return 0;
    }

    //假设起始码为三个字节长度
    nalu->startcodeprefix_len = 3;

    size_t read_result = 0;
    //  step：1 先从码流中读取三个字节
    if ((read_result       = fread(nalu_buffer, 1, 3, h264bitstream)) != 3) {
        free(nalu_buffer);
        return 0;
    }
    //  step：2 判断是否满足00 00 01
    is_three_byte_sequence = FindStartCode2(nalu_buffer);
    //  step：3 如果不满足的话,再读一个进buf判断是否为00 00 00 01
    if (is_three_byte_sequence != 1) {
        if (1 != fread(nalu_buffer + 3, 1, 1, h264bitstream)) {
            free(nalu_buffer);
            return 0;
        }

        is_four_byte_sequence = FindStartCode3(nalu_buffer);
        // step：4 也不是00 00 00 01的话,则退出本次查找;否则记录NALU开始的下标与对应起始码长度
        if (is_four_byte_sequence != 1) {
            free(nalu_buffer);
            return -1;
        } else {
            nalu_index = 4;
            nalu->startcodeprefix_len = 4;
        }
    }
    else {
        // step：5 满足三字节的起始码,记录NALU开始的下标与对应起始码长度
        nalu->startcodeprefix_len = 3;
        nalu_index = 3;
    }


     // 来到这里说明找到了首个起始码,开始循环读NALU数据与找起始码(pos指向NALU的第一个字节数据)
    //step : 6 先重置这些是否找到起始码的标志位
    StartCodeFound         = 0;
    is_three_byte_sequence = 0;
    is_four_byte_sequence  = 0;

    while (!StartCodeFound) {
        //15 由于最后一个NALU没有下一个起始码,所以当读到末尾时,直接将pos-1后减去起始码就是数据的长度
        //非0表示文件尾,0不是
        if (feof(h264bitstream) != 0) {

            //最后一个NALU数据的长度(减1是因为最后一个nalu时，会一个个读到buf，pos会自增，直到读到eof)
            nalu->len = (nalu_index - 1) - nalu->startcodeprefix_len;
            //该NALU数据拷贝至该结构体成员buf中保存
            memcpy(nalu->buf, &nalu_buffer[nalu->startcodeprefix_len], nalu->len);
            //用该NALU的头部数据给赋给雷神自定义的NALU结构体中
            nalu->forbidden_bit     = nalu->buf[0] & 0x80;
            nalu->nal_reference_idc = nalu->buf[0] & 0x60;
            nalu->nal_unit_type     = (nalu->buf[0]) & 0x1f;
            //每次获取完一个NALU都清空该NALU的数据(下面的获取也一样)
            free(nalu_buffer);
            //返回文件最后一个字节的下标，在最后一帧pos代表eof(并不是end,pos下标才对应eof)
            return nalu_index - 1;
            // 这样返回才是不带起始码长度，上面pos-1是带的
            //return nalu->len;
        }
        // 7 往Buf一字节 一字节的读数据 (注:pos之前有起始码,所以pos开始存，然后pos自增1)
        nalu_buffer[nalu_index++] = fgetc(h264bitstream);
        // 8 判断是否为四位起始码.例如0 0 0 1 2(实际上pos=5),此时从0 0 1 2开始判断,所以取Buf[nalu_index - 4]元素开始判断
        is_four_byte_sequence = FindStartCode3(&nalu_buffer[nalu_index - 4]);
        if (is_four_byte_sequence != 1) {
            //9 不是则判断是否是三位起始码.当前下标减去3即可(本来减2,但上面pos++了)
            is_three_byte_sequence = FindStartCode2(&nalu_buffer[nalu_index - 3]);
        }
        //10 若找到下一个起始码则退出(证明知道了一个NALU数据的长度嘛)
        StartCodeFound        = (is_three_byte_sequence == 1 || is_four_byte_sequence == 1);
    }

    //11 判断下一个起始码是3还是4(他这里用info3,代表4位,所以一会需要将文件指针回调rewind个字节,为了下一次判断)
    //当然你也可以用info2
    rewind = (is_four_byte_sequence == 1) ? -4 : -3;

    //12 回调文件指针
    if (0 != fseek(h264bitstream, rewind, SEEK_CUR)) {
        free(nalu_buffer);
        printf("get_annexb_nalu: Cannot fseek in the bit stream file");
    }

    //13 开始获取NALU的数据
    nalu->len = (nalu_index + rewind) - nalu->startcodeprefix_len;//注：rewind为负数,加相当于减,然后再减去上一起始码就是NALU的数据长度
    memcpy(nalu->buf, &nalu_buffer[nalu->startcodeprefix_len], nalu->len);//从起始码开始拷贝len长度的NALU数据至自定义结构体的buf中
    nalu->forbidden_bit     = nalu->buf[0] & 0x80; //用该NALU的头部数据给赋给雷神自定义的NALU结构体中
    nalu->nal_reference_idc = nalu->buf[0] & 0x60;
    nalu->nal_unit_type     = (nalu->buf[0]) & 0x1f;
    free(nalu_buffer);
    //14 返回当前文件指针位置,即下一起始码的首字节(rewind为负数)  至此,一个NALU的数据获取完毕
    return (nalu_index + rewind);
}


/**
 * 解析码流h264
 * @param url
 * @return   成功返回0;失败返回-1
 */
int simplest_h264_parser(char *url) {

    //FILE *myout=fopen("output_log.txt","wb+");
    //用于输出屏幕的文件指针,你可以认为该文件是屏幕
    FILE *myout = stdout;

    //1 打开文件
    h264bitstream = fopen(url, "rb+");
    if (h264bitstream == NULL) {
        perror("Open file error\n");
        return -1;
    }

    //2 开辟nalu结构体以及用于存储nalu数据的成员nalu->buf
    //雷神自定义的NALU数据,额外包含头部与长度信息
    NALU_t *nalu;
    nalu = (NALU_t *) calloc(1, sizeof(NALU_t));
    if (nalu == NULL) {
        perror("Alloc NALU Error\n");
        return -1;
    }
    //临时缓存,足够大于一个NALU的字节数即可
    int buffer_size = 100000;
    nalu->max_size = buffer_size;
    nalu->buf      = (char *) calloc(buffer_size, sizeof(char));
    if (nalu->buf == NULL) {
        free(nalu);
        printf("AllocNALU: n->buf");
        return -1;
    }

    //累加每一次的偏移量,用于记录每个NALU的起始地址,雷神的这个偏移量是包括对应的起始码(3或者4字节),即显示的POS字段
    int data_offset = 0;
    //NALU的数量,从0开始算
    int nal_num     = 0;
    //接收返回值,文件指针的位置,也就是下一起始码首字节,或者说下一NALU的偏移地址
    int data_length;
    printf("-----+-------- NALU Table ------+---------+\n");
    printf(" NUM |    POS  |    IDC |  TYPE |   LEN   |\n");
    printf("-----+---------+--------+-------+---------+\n");

    //3 循环读取码流获取NALU
    while (!feof(h264bitstream)) {
        data_length = get_annexb_nalu(nalu);

        //4 获取 NALU的 类型
        char type_str[20] = {0};

        switch (nalu->nal_unit_type) {
            case NALU_TYPE_SLICE:
                sprintf(type_str, "SLICE");
                break;
            case NALU_TYPE_DPA:
                sprintf(type_str, "DPA");
                break;
            case NALU_TYPE_DPB:
                sprintf(type_str, "DPB");
                break;
            case NALU_TYPE_DPC:
                sprintf(type_str, "DPC");
                break;
            case NALU_TYPE_IDR:
                sprintf(type_str, "IDR");
                break;
            case NALU_TYPE_SEI:
                sprintf(type_str, "SEI");
                break;
            case NALU_TYPE_SPS:
                sprintf(type_str, "SPS");
                break;
            case NALU_TYPE_PPS:
                sprintf(type_str, "PPS");
                break;
            case NALU_TYPE_AUD:
                sprintf(type_str, "AUD");
                break;
            case NALU_TYPE_EOSEQ:
                sprintf(type_str, "EOSEQ");
                break;
            case NALU_TYPE_EOSTREAM:
                sprintf(type_str, "EOSTREAM");
                break;
            case NALU_TYPE_FILL:
                sprintf(type_str, "FILL");
                break;
        }
        //5 获取NALU的IDC即优先级
        char idc_str[20] = {0};
        switch (nalu->nal_reference_idc >> 5) {
            case NALU_PRIORITY_DISPOSABLE:
                sprintf(idc_str, "DISPOS");
                break;
            case NALU_PRIRITY_LOW:
                sprintf(idc_str, "LOW");
                break;
            case NALU_PRIORITY_HIGH:
                sprintf(idc_str, "HIGH");
                break;
            case NALU_PRIORITY_HIGHEST:
                sprintf(idc_str, "HIGHEST");
                break;
        }

        //6 输出nal个数 此时数据的偏移量,优先级,NALU类型,NALU数据的长度
        fprintf(myout, "%5d| %8d| %7s| %6s| %8d|\n", nal_num, data_offset, idc_str, type_str, nalu->len);

        //7 记录下一NALU的偏移地址,即计算后,该偏移地址就是下一NALU的偏移地址.例如显示0后,0+29就是下一NALU的偏移地址
        data_offset = data_offset + data_length;
        nal_num++;
    }

    //8 Free掉nalu与nalu->buf
    if (nalu != NULL) {
        if (nalu->buf != NULL) {
            free(nalu->buf);
            nalu->buf = NULL;
        }
        free(nalu);
        nalu = NULL;
    }
    return 0;
}


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
int generate_h264_stream() {
    // app_name                           = argv[0];
    char *in_filename  = VIDEO_PATH;
    char *out_filename = H264_FILE_PATH;
    FILE *of           = NULL;

    AVPacket *pkt = av_packet_alloc();

    int ret                = 0;
    int video_stream_index = -1;
    int is_annexb          = 1;

    AVFormatContext *ifmt_ctx = avformat_alloc_context();

    AVBSFContext *bsf_ctx = NULL;


    if ((ret = open_input(&ifmt_ctx, in_filename)) < 0) {
        fprintf(stderr, "open_input failed, ret=%d\n", ret);
        goto end;
    }
    if ((ret = open_output(&of, out_filename)) < 0) {
        fprintf(stderr, "open_output failed, ret=%d\n", ret);
        goto end;
    }


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
        if ((ret = open_bitstream_filter(ifmt_ctx->streams[video_stream_index], &bsf_ctx, "h264_mp4toannexb")) < 0) {
            fprintf(stderr, "open_bitstream_filter failed, ret=%d\n", ret);
            goto end;
        }
    }

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != video_stream_index) {
            av_packet_unref(pkt);
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
    if (pkt != NULL)
        av_packet_free(&pkt);
    if (ifmt_ctx)
        avformat_close_input(&ifmt_ctx);
    if (bsf_ctx != NULL) {
        av_bsf_free(&bsf_ctx);
    }
    if (of != NULL) {
        fclose(of);
    }
    fprintf(stdout, "convert finished, ret=%d\n", ret);
    return 0;
}


int main() {

    generate_h264_stream();
    puts("=========================");

    simplest_h264_parser(H264_FILE_PATH);

    return 0;
}

