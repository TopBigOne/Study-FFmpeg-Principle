### Bitstream Filter

* Bitstream Filter的主要目的是对数据进行格式转换，使它能够被解码器处理（比如HEVC QSV的解码器）。
* Bitstream Filter对已编码的码流进行操作，不涉及解码过程。
* 使用ffmpeg -bsfs命令可以查看ffmpeg工具支持的Bitstream Filter类型。
* 使用ff*工具的-bsf选项来指定对具体流的Bitstream Filter，使用逗号分隔的多个filter，如果filter有参数，参数名和参数值跟在filter名称后面，如下面的形式。

### Bitstream Filter API介绍
```c
// Query
const AVBitStreamFilter *av_bsf_next(void **opaque);
const AVBitStreamFilter *av_bsf_get_by_name(const char *name);

// Setup
int av_bsf_alloc(const AVBitStreamFilter *filter, AVBSFContext **ctx);
int av_bsf_init(AVBSFContext *ctx);

// Usage
int av_bsf_send_packet(AVBSFContext *ctx, AVPacket *pkt);
int av_bsf_receive_packet(AVBSFContext *ctx, AVPacket *pkt);

// Cleanup
void av_bsf_free(AVBSFContext **ctx);
```

```shell
ffmpeg -i juren-30s.mp4 -codec copy -bsf:v h264_mp4toannexb doc/OUTPUT.ts
```
-------
###  示例： 从多媒体文件比如mp4或者ts中分离出H.264的码流的工作
* 在分离ts码流的时候，直接存储 AVPacket 即可。
* 对于mp4中解封装出来的AVPacket需要经过 **h264_mp4toannexb** 的bitstream filter的处理。
* ffmpeg中定义的H.264的封装格式是 **annexb** 格式的。

 